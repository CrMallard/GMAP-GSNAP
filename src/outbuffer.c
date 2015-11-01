static char rcsid[] = "$Id: outbuffer.c 173039 2015-08-31 19:12:10Z twu $";
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "outbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_PTHREAD
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>		/* Needed to define pthread_t on Solaris */
#endif
#include <pthread.h>
#endif

#include "assert.h"
#include "bool.h"
#include "mem.h"
#include "samheader.h"


/* MPI processing */
#ifdef DEBUGM
#define debugm(x) x
#else
#define debugm(x)
#endif

#ifdef DEBUG
#define debug(x) x
#else
#define debug(x)
#endif

#ifdef DEBUG1
#define debug1(x) x
#else
#define debug1(x)
#endif


/* sam-to-bam conversions always need the headers */
#define SAM_HEADERS_ON_EMPTY_FILES 1

static int argc;
static char **argv;
static int optind_save;

static Univ_IIT_T chromosome_iit;
static bool any_circular_p;
static int nworkers;
static bool orderedp;
static bool quiet_if_excessive_p;

#ifdef GSNAP
static bool output_sam_p;
#else
static Printtype_T printtype;
static Sequence_T usersegment;
#endif

static bool sam_headers_p;
static char *sam_read_group_id;
static char *sam_read_group_name;
static char *sam_read_group_library;
static char *sam_read_group_platform;

static bool appendp;
static char *output_file;
static char *split_output_root;
static char *failedinput_root;

#ifdef USE_MPI
static MPI_File *outputs;
#ifdef GSNAP
static MPI_File output_failedinput_1;
static MPI_File output_failedinput_2;
#else
static MPI_File output_failedinput;
#endif


#else
static char *write_mode;
static FILE **outputs = NULL;
#ifdef GSNAP
static FILE *output_failedinput_1;
static FILE *output_failedinput_2;
#else
static FILE *output_failedinput;
#endif

#endif



/* Taken from Univ_IIT_dump_sam */
static void
dump_sam_usersegment (FILE *fp, Sequence_T usersegment,
		      char *sam_read_group_id, char *sam_read_group_name,
		      char *sam_read_group_library, char *sam_read_group_platform) {

  fprintf(fp,"@SQ\tSN:%s",Sequence_accession(usersegment));
  fprintf(fp,"\tLN:%u",Sequence_fulllength(usersegment));
  fprintf(fp,"\n");

  if (sam_read_group_id != NULL) {
    fprintf(fp,"@RG\tID:%s",sam_read_group_id);
    if (sam_read_group_platform != NULL) {
      fprintf(fp,"\tPL:%s",sam_read_group_platform);
    }
    if (sam_read_group_library != NULL) {
      fprintf(fp,"\tLB:%s",sam_read_group_library);
    }
    fprintf(fp,"\tSM:%s",sam_read_group_name);
    fprintf(fp,"\n");
  }

  return;
}

#ifndef GSNAP
static void
print_gff_header (FILE *fp, int argc, char **argv, int optind) {
  char **argstart;
  int c;

  fprintf(fp,"##gff-version   3\n");
  fprintf(fp,"# Generated by GMAP version %s using call: ",PACKAGE_VERSION);
  argstart = &(argv[-optind]);
  for (c = 0; c < argc + optind; c++) {
    fprintf(fp," %s",argstart[c]);
  }
  fprintf(fp,"\n");
  return;
}
#endif


static void
print_file_headers (
#ifdef USE_MPI
		    MPI_File output
#else
		    FILE *output
#endif
		    ) {
#ifdef GSNAP
  if (output_sam_p == true && sam_headers_p == true) {
    SAM_header_print_HD(output,nworkers,orderedp);
    SAM_header_print_PG(output,argc,argv,optind_save);
    Univ_IIT_dump_sam(output,chromosome_iit,sam_read_group_id,sam_read_group_name,
		      sam_read_group_library,sam_read_group_platform);
  }

#else
  if (printtype == GFF3_GENE || printtype == GFF3_MATCH_CDNA || printtype == GFF3_MATCH_EST) {
    print_gff_header(output,argc,argv,optind_save);
      
#ifndef PMAP
  } else if (printtype == SAM && sam_headers_p == true) {
    if (usersegment != NULL) {
      dump_sam_usersegment(output,usersegment,sam_read_group_id,sam_read_group_name,
			   sam_read_group_library,sam_read_group_platform);
    } else {
      SAM_header_print_HD(output,nworkers,orderedp);
      SAM_header_print_PG(output,argc,argv,optind_save);
      Univ_IIT_dump_sam(output,chromosome_iit,sam_read_group_id,sam_read_group_name,
			sam_read_group_library,sam_read_group_platform);
    }
#endif

  }
#endif

  return;
}


static void
failedinput_open (char *failedinput_root) {
  char *filename;

#ifdef GSNAP
  filename = (char *) MALLOC((strlen(failedinput_root)+strlen(".1")+1) * sizeof(char));
  sprintf(filename,"%s.1",failedinput_root);

#ifdef USE_MPI
  if (appendp == true) {
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY | MPI_MODE_APPEND,
                  MPI_INFO_NULL,&output_failedinput_1);
  } else {
    /* Need to remove existing file, if any */
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY | MPI_MODE_DELETE_ON_CLOSE,
		  MPI_INFO_NULL,&output_failedinput_1);
    MPI_File_close(&output_failedinput_1);
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY,
		  MPI_INFO_NULL,&output_failedinput_1);
  }
#else
  if ((output_failedinput_1 = fopen(filename,write_mode)) == NULL) {
    fprintf(stderr,"Cannot open file %s for writing\n",filename);
    exit(9);
  }
#endif

  /* Re-use filename, since it is the same length */
  sprintf(filename,"%s.2",failedinput_root);
#ifdef USE_MPI
  if (appendp == true) {
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY | MPI_MODE_APPEND,
		  MPI_INFO_NULL,&output_failedinput_2);
  } else {
    /* Need to remove existing file, if any */
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY | MPI_MODE_DELETE_ON_CLOSE,
		  MPI_INFO_NULL,&output_failedinput_2);
    MPI_File_close(&output_failedinput_2);
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY,
		  MPI_INFO_NULL,&output_failedinput_2);
  }
#else
  if ((output_failedinput_2 = fopen(filename,write_mode)) == NULL) {
    fprintf(stderr,"Cannot open file %s for writing\n",filename);
    exit(9);
  }
#endif
  FREE(filename);

#else  /* GMAP */
  filename = (char *) MALLOC((strlen(failedinput_root)+1) * sizeof(char));
  sprintf(filename,"%s",failedinput_root);
#ifdef USE_MPI
  if (appendp == true) {
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY | MPI_MODE_APPEND,
		  MPI_INFO_NULL,&output_failedinput);
  } else {
    /* Need to remove existing file, if any */
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY | MPI_MODE_DELETE_ON_CLOSE,
		  MPI_INFO_NULL,&output_failedinput);
    MPI_File_close(&output_failedinput);
    MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE | MPI_MODE_WRONLY,
		  MPI_INFO_NULL,&output_failedinput);
  }
#else
  if ((output_failedinput = fopen(filename,write_mode)) == NULL) {
    fprintf(stderr,"Cannot open file %s for writing\n",filename);
    exit(9);
  }
#endif
  FREE(filename);

#endif	/* GSNAP */

  return;
}


void
Outbuffer_setup (int argc_in, char **argv_in, int optind_in,
		 Univ_IIT_T chromosome_iit_in, bool any_circular_p_in,
		 int nworkers_in, bool orderedp_in, bool quiet_if_excessive_p_in,
#ifdef GSNAP
		 bool output_sam_p_in, 
#else
		 Printtype_T printtype_in, Sequence_T usersegment_in,
#endif
		 bool sam_headers_p_in, char *sam_read_group_id_in, char *sam_read_group_name_in,
		 char *sam_read_group_library_in, char *sam_read_group_platform_in,
		 bool appendp_in, char *output_file_in, char *split_output_root_in, char *failedinput_root_in) {
#ifdef USE_MPI
  SAM_split_output_type split_output;
#endif

  
  argc = argc_in;
  argv = argv_in;
  optind_save = optind_in;

  chromosome_iit = chromosome_iit_in;
  any_circular_p = any_circular_p_in;

  nworkers = nworkers_in;
  orderedp = orderedp_in;
  quiet_if_excessive_p = quiet_if_excessive_p_in;

#ifdef GSNAP
  output_sam_p = output_sam_p_in;
#else
  printtype = printtype_in;
  usersegment = usersegment_in;
#endif

  sam_headers_p = sam_headers_p_in;
  sam_read_group_id = sam_read_group_id_in;
  sam_read_group_name = sam_read_group_name_in;
  sam_read_group_library = sam_read_group_library_in;
  sam_read_group_platform = sam_read_group_platform_in;

  appendp = appendp_in;
  split_output_root = split_output_root_in;
  output_file = output_file_in;


  /************************************************************************/
  /* Output files */
  /************************************************************************/

#ifdef USE_MPI
  /* All processes need to run MPI_File_open, and need to open all files now */
  outputs = (MPI_File *) CALLOC_KEEP(1+N_SPLIT_OUTPUTS,sizeof(MPI_File));
  if (split_output_root != NULL) {
    for (split_output = 1; split_output <= N_SPLIT_OUTPUTS; split_output++) {
      outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
      print_file_headers(outputs[split_output]);
#endif
    }

  } else if (output_file != NULL) {
    outputs[0] = SAM_header_open_file(OUTPUT_NONE,/*split_output_root*/output_file,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
    print_file_headers(outputs[0]);
#endif
    for (split_output = 1; split_output <= N_SPLIT_OUTPUTS; split_output++) {
      outputs[split_output] = outputs[0];
    }

  } else {
    /* Write to stdout */
    outputs[0] = (MPI_File) NULL;
#ifdef SAM_HEADERS_ON_EMPTY_FILES
    print_file_headers(outputs[0]);
#endif
    for (split_output = 1; split_output <= N_SPLIT_OUTPUTS; split_output++) {
      outputs[split_output] = (MPI_File) NULL;
    }
  }

#else
  /* Only the output thread needs to run fopen, and can open files when needed */
  if (appendp == true) {
    write_mode = "a";
  } else {
    write_mode = "w";
  }
  outputs = (FILE **) CALLOC_KEEP(1+N_SPLIT_OUTPUTS,sizeof(FILE *));
#endif


  /************************************************************************/
  /* Failed input files */
  /************************************************************************/

  failedinput_root = failedinput_root_in;
  if (failedinput_root == NULL) {
#ifdef GSNAP
    output_failedinput_1 = output_failedinput_2 = NULL;
#else
    output_failedinput = NULL;
#endif
  } else {
    failedinput_open(failedinput_root);
  }

  return;
}


void
Outbuffer_cleanup () {
  FREE_KEEP(outputs);		/* Matches CALLOC_KEEP in Outbuffer_setup */
  return;
}


typedef struct RRlist_T *RRlist_T;
struct RRlist_T {
  int id;
  Filestring_T fp;
#ifdef GSNAP
  Filestring_T fp_failedinput_1;
  Filestring_T fp_failedinput_2;
#else
  Filestring_T fp_failedinput;
#endif
  RRlist_T next;
};


#ifdef DEBUG1
static void
RRlist_dump (RRlist_T head, RRlist_T tail) {
  RRlist_T this;

  printf("head %p\n",head);
  for (this = head; this != NULL; this = this->next) {
    printf("%p: next %p\n",this,this->next);
  }
  printf("tail %p\n",tail);
  printf("\n");
  return;
}
#endif


/* Returns new tail */
static RRlist_T
RRlist_push (RRlist_T *head, RRlist_T tail, Filestring_T fp,
#ifdef GSNAP
	     Filestring_T fp_failedinput_1, Filestring_T fp_failedinput_2
#else
	     Filestring_T fp_failedinput
#endif
	     ) {
  RRlist_T new;

  new = (RRlist_T) MALLOC_OUT(sizeof(*new)); /* Called by worker thread */
  new->fp = fp;
#ifdef GSNAP
  new->fp_failedinput_1 = fp_failedinput_1;
  new->fp_failedinput_2 = fp_failedinput_2;
#else
  new->fp_failedinput = fp_failedinput;
#endif
  new->next = (RRlist_T) NULL;
  
  if (*head == NULL) {		/* Equivalent to tail == NULL, but using *head avoids having to set tail in RRlist_pop */
    *head = new;
  } else {
    tail->next = new;
  }

  return new;
}


/* Returns new head */
static RRlist_T
RRlist_pop (RRlist_T head, Filestring_T *fp,
#ifdef GSNAP
	    Filestring_T *fp_failedinput_1, Filestring_T *fp_failedinput_2
#else
	    Filestring_T *fp_failedinput
#endif
	    ) {
  RRlist_T newhead;

  *fp = head->fp;
#ifdef GSNAP
  *fp_failedinput_1 = head->fp_failedinput_1;
  *fp_failedinput_2 = head->fp_failedinput_2;
#else
  *fp_failedinput = head->fp_failedinput;
#endif

  newhead = head->next;

  FREE_OUT(head);		/* Called by outbuffer thread */
  return newhead;
}


static RRlist_T
RRlist_insert (RRlist_T list, int id, Filestring_T fp,
#ifdef GSNAP	       
	       Filestring_T fp_failedinput_1, Filestring_T fp_failedinput_2
#else
	       Filestring_T fp_failedinput
#endif
	       ) {
  RRlist_T *p;
  RRlist_T new;
  
  p = &list;
  while (*p != NULL && id > (*p)->id) {
    p = &(*p)->next;
  }

  new = (RRlist_T) MALLOC_OUT(sizeof(*new));
  new->id = id;
  new->fp = fp;
#ifdef GSNAP
  new->fp_failedinput_1 = fp_failedinput_1;
  new->fp_failedinput_2 = fp_failedinput_2;
#else
  new->fp_failedinput = fp_failedinput;
#endif
  
  new->next = *p;
  *p = new;
  return list;
}

/* Returns new head */
static RRlist_T
RRlist_pop_id (RRlist_T head, int *id, Filestring_T *fp,
#ifdef GSNAP
	       Filestring_T *fp_failedinput_1, Filestring_T *fp_failedinput_2
#else
	       Filestring_T *fp_failedinput
#endif
	       ) {
  RRlist_T newhead;

  *id = head->id;
  *fp = head->fp;
#ifdef GSNAP
  *fp_failedinput_1 = head->fp_failedinput_1;
  *fp_failedinput_2 = head->fp_failedinput_2;
#else
  *fp_failedinput = head->fp_failedinput;
#endif

  newhead = head->next;

  FREE_OUT(head);		/* Called by outbuffer thread */
  return newhead;
}


#define T Outbuffer_T
struct T {

#ifdef HAVE_PTHREAD
  pthread_mutex_t lock;
#endif

  unsigned int output_buffer_size;
  unsigned int nread;
  unsigned int ntotal;
  unsigned int nbeyond;		/* MPI request that is beyond the given inputs */
  unsigned int nprocessed;

  RRlist_T head;
  RRlist_T tail;
  
#ifdef HAVE_PTHREAD
  pthread_cond_t filestring_avail_p;
#endif
};


/************************************************************************
 *   File routines
 ************************************************************************/


T
Outbuffer_new (unsigned int output_buffer_size, unsigned int nread) {
  T new = (T) MALLOC_KEEP(sizeof(*new));

#ifdef HAVE_PTHREAD
  pthread_mutex_init(&new->lock,NULL);
#endif

  new->output_buffer_size = output_buffer_size;
  new->nread = nread;

  /* Set to infinity until all reads are input.  Used for Pthreads version */
  new->ntotal = (unsigned int) -1U;

  new->nbeyond = 0;
  new->nprocessed = 0;

  new->head = (RRlist_T) NULL;
  new->tail = (RRlist_T) NULL;

#ifdef HAVE_PTHREAD
  pthread_cond_init(&new->filestring_avail_p,NULL);
#endif

  return new;
}



#ifndef USE_MPI
/* Open empty files, and add SAM headers if SAM_HEADERS_ON_EMPTY_FILES is set */
static void
touch_all_single_outputs (FILE **outputs, char *split_output_root, bool appendp) {
  SAM_split_output_type split_output;

  split_output = 1;
  while (split_output <= N_SPLIT_OUTPUTS_SINGLE_STD) {
    if (outputs[split_output] == NULL) {
      outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
      print_file_headers(outputs[split_output]);
#endif
    }
    split_output++;
  }

  if (any_circular_p == false) {
    split_output = N_SPLIT_OUTPUTS_SINGLE_TOCIRC + 1;
  } else {
    while (split_output <= N_SPLIT_OUTPUTS_SINGLE_TOCIRC) {
      if (outputs[split_output] == NULL) {
	outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
        print_file_headers(outputs[split_output]);
#endif
      }
      split_output++;
    }
  }

  if (quiet_if_excessive_p == true) {
    while (split_output <= N_SPLIT_OUTPUTS_SINGLE) {
      if (outputs[split_output] == NULL) {
	outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
        print_file_headers(outputs[split_output]);
#endif
      }
      split_output++;
    }
  }

  return;
}
#endif


#ifndef USE_MPI
/* Open empty files, and add SAM headers if SAM_HEADERS_ON_EMPTY_FILES is set */
static void
touch_all_paired_outputs (FILE **outputs, char *split_output_root, bool appendp) {
  SAM_split_output_type split_output;

  split_output = N_SPLIT_OUTPUTS_SINGLE + 1;
  while (split_output <= N_SPLIT_OUTPUTS_STD) {
    if (outputs[split_output] == NULL) {
      outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
      print_file_headers(outputs[split_output]);
#endif
    }
    split_output++;
  }

  if (any_circular_p == false) {
    split_output = N_SPLIT_OUTPUTS_TOCIRC + 1;
  } else {
    while (split_output <= N_SPLIT_OUTPUTS_TOCIRC) {
      if (outputs[split_output] == NULL) {
	outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
        print_file_headers(outputs[split_output]);
#endif
      }
      split_output++;
    }
  }

  if (quiet_if_excessive_p == true) {
    while (split_output <= N_SPLIT_OUTPUTS) {
      if (outputs[split_output] == NULL) {
	outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
#ifdef SAM_HEADERS_ON_EMPTY_FILES
        print_file_headers(outputs[split_output]);
#endif
      }
      split_output++;
    }
  }

  return;
}
#endif


#ifndef USE_MPI
static bool
paired_outputs_p (FILE **outputs) {
  SAM_split_output_type split_output;

  split_output = N_SPLIT_OUTPUTS_SINGLE + 1;
  while (split_output <= N_SPLIT_OUTPUTS) {
    if (outputs[split_output] != NULL) {
      return true;
    }
    split_output++;
  }

  return false;
}
#endif


#ifndef USE_MPI
static void
touch_all_files (FILE **outputs, char *split_output_root, bool appendp) {
  touch_all_single_outputs(outputs,split_output_root,appendp);
  if (paired_outputs_p(outputs) == true) {
    touch_all_paired_outputs(outputs,split_output_root,appendp);
  }
  return;
}
#endif



void
Outbuffer_close_files () {
  SAM_split_output_type split_output;

  if (failedinput_root != NULL) {
#ifdef USE_MPI
#ifdef GSNAP
    MPI_File_close(&output_failedinput_1);
    MPI_File_close(&output_failedinput_2);
#else
    MPI_File_close(&output_failedinput);
#endif
    
#else
#ifdef GSNAP
    fclose(output_failedinput_1);
    fclose(output_failedinput_2);
#else
    fclose(output_failedinput);
#endif
#endif

  }

#ifdef USE_MPI
  if (split_output_root != NULL) {
    for (split_output = 1; split_output <= N_SPLIT_OUTPUTS; split_output++) {
      MPI_File_close(&(outputs[split_output]));
    }
  } else if (output_file != NULL) {
    MPI_File_close(&(outputs[0]));
  } else {
    /* Wrote to stdout */
  }

#else
  if (split_output_root != NULL) {
    touch_all_files(outputs,split_output_root,appendp);

    for (split_output = 1; split_output <= N_SPLIT_OUTPUTS; split_output++) {
      if (outputs[split_output] != NULL) {
	fclose(outputs[split_output]);
      }
    }
  } else if (output_file != NULL) {
    fclose(outputs[0]);
  } else {
    /* Wrote to stdout */
  }
#endif

  FREE_KEEP(outputs);

  return;
}


void
Outbuffer_free (T *old) {

  if (*old) {
#ifdef HAVE_PTHREAD
    pthread_cond_destroy(&(*old)->filestring_avail_p);
    pthread_mutex_destroy(&(*old)->lock);
#endif

    FREE_KEEP(*old);
  }

  return;
}


unsigned int
Outbuffer_nread (T this) {
  return this->nread;
}

unsigned int
Outbuffer_nbeyond (T this) {
  return this->nbeyond;
}


void
Outbuffer_add_nread (T this, unsigned int nread) {

#ifdef HAVE_PTHREAD
  pthread_mutex_lock(&this->lock);
#endif

  if (nread == 0) {
    /* Finished reading, so able to determine total reads in input */
    this->ntotal = this->nread;
    debug(fprintf(stderr,"__Outbuffer_add_nread added 0 reads, so setting ntotal to be %u\n",this->ntotal));

#ifdef HAVE_PTHREAD
    pthread_cond_signal(&this->filestring_avail_p);
#endif

  } else {
    this->nread += nread;
#ifdef USE_MPI
    this->ntotal = this->nread;
#endif
    debug(fprintf(stderr,"__Outbuffer_add_nread added %d read, now %d\n",nread,this->nread));
  }

#ifdef HAVE_PTHREAD
  pthread_mutex_unlock(&this->lock);
#endif

  return;
}


void
Outbuffer_add_nbeyond (T this) {

#ifdef HAVE_PTHREAD
  pthread_mutex_lock(&this->lock);
#endif

  this->nbeyond += 1;

#ifdef HAVE_PTHREAD
  pthread_cond_signal(&this->filestring_avail_p);
  pthread_mutex_unlock(&this->lock);
#endif

  return;
}


#ifdef GSNAP
void
Outbuffer_put_filestrings (T this, Filestring_T fp, Filestring_T fp_failedinput_1, Filestring_T fp_failedinput_2) {

#ifdef HAVE_PTHREAD
  pthread_mutex_lock(&this->lock);
#endif

  this->tail = RRlist_push(&this->head,this->tail,fp,fp_failedinput_1,fp_failedinput_2);
  debug1(RRlist_dump(this->head,this->tail));
  this->nprocessed += 1;

#ifdef HAVE_PTHREAD
  debug(printf("Signaling that filestring is available\n"));
  pthread_cond_signal(&this->filestring_avail_p);
  pthread_mutex_unlock(&this->lock);
#endif

  return;
}

#else
void
Outbuffer_put_filestrings (T this, Filestring_T fp, Filestring_T fp_failedinput) {

#ifdef HAVE_PTHREAD
  pthread_mutex_lock(&this->lock);
#endif

  this->tail = RRlist_push(&this->head,this->tail,fp,fp_failedinput);
  debug1(RRlist_dump(this->head,this->tail));
  this->nprocessed += 1;

#ifdef HAVE_PTHREAD
  debug(printf("Signaling that filestring is available\n"));
  pthread_cond_signal(&this->filestring_avail_p);
  pthread_mutex_unlock(&this->lock);
#endif

  return;
}
#endif



#ifdef GSNAP
void
Outbuffer_print_filestrings (Filestring_T fp, Filestring_T fp_failedinput_1, Filestring_T fp_failedinput_2) {
  SAM_split_output_type split_output;
#ifdef USE_MPI
  MPI_File output;
#else
  FILE *output;
#endif

#ifdef USE_MPI
  split_output = Filestring_split_output(fp);
  output = outputs[split_output];

#else
  if (split_output_root != NULL) {
    split_output = Filestring_split_output(fp);
    if ((output = outputs[split_output]) == NULL) {
      output = outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
      if (split_output == OUTPUT_NONE && split_output_root != NULL) {
	/* Don't print file headers, since no output will go to
	   stdout.  Must be a nomapping when --nofails is specified */
      } else {
	print_file_headers(output);
      }
    }
  } else if ((output = outputs[0]) == NULL) {
    if (output_file == NULL) {
      output = outputs[0] = stdout;
      print_file_headers(stdout);
    } else {
      output = outputs[0] = SAM_header_open_file(/*split_output*/OUTPUT_NONE,output_file,appendp);
      print_file_headers(output);
    }
  }
#endif

#ifdef USE_MPI
  /* Prevents output from being broken up */
  Filestring_stringify(fp);
#endif
  Filestring_print(output,fp);
  Filestring_free(&fp);

  if (failedinput_root != NULL) {
    if (fp_failedinput_1 != NULL) {
#ifdef USE_MPI
      Filestring_stringify(fp_failedinput_1);
#endif
      Filestring_print(output_failedinput_1,fp_failedinput_1);
      Filestring_free(&fp_failedinput_1);
    }
    if (fp_failedinput_2 != NULL) {
#ifdef USE_MPI
      Filestring_stringify(fp_failedinput_2);
#endif
      Filestring_print(output_failedinput_2,fp_failedinput_2);
      Filestring_free(&fp_failedinput_2);
    }
  }

  return;
}

#else
void
Outbuffer_print_filestrings (Filestring_T fp, Filestring_T fp_failedinput) {
  SAM_split_output_type split_output;
#ifdef USE_MPI
  MPI_File output;
#else
  FILE *output;
#endif

#ifdef USE_MPI
  split_output = Filestring_split_output(fp);
  output = outputs[split_output];

#else
  if (split_output_root != NULL) {
    split_output = Filestring_split_output(fp);
    if ((output = outputs[split_output]) == NULL) {
      output = outputs[split_output] = SAM_header_open_file(split_output,split_output_root,appendp);
      if (split_output == OUTPUT_NONE && split_output_root != NULL) {
	/* Don't print file headers, since no output will go to
	   stdout.  Must be a nomapping when --nofails is specified */
      } else {
	print_file_headers(output);
      }
    }

  } else if ((output = outputs[0]) == NULL) {
    if (output_file == NULL) {
      output = outputs[0] = stdout;
      print_file_headers(stdout);
    } else {
      output = outputs[0] = SAM_header_open_file(/*split_output*/OUTPUT_NONE,output_file,appendp);
      print_file_headers(output);
    }
  }
#endif

#ifdef USE_MPI
  Filestring_stringify(fp);
#endif
  Filestring_print(output,fp);
  Filestring_free(&fp);

  if (failedinput_root != NULL) {
    if (fp_failedinput != NULL) {
#ifdef USE_MPI
      Filestring_stringify(fp_failedinput);
#endif
      Filestring_print(output_failedinput,fp_failedinput);
      Filestring_free(&fp_failedinput);
    }
  }

  return;
}

#endif



void *
Outbuffer_thread_anyorder (void *data) {
  T this = (T) data;
  unsigned int output_buffer_size = this->output_buffer_size;
  unsigned int noutput = 0, ntotal, nbeyond;
  Filestring_T fp;
#ifdef GSNAP
  Filestring_T fp_failedinput_1, fp_failedinput_2;
#else
  Filestring_T fp_failedinput;
#endif
  
  /* Obtain this->ntotal while locked, to prevent race between output thread and input thread */
#ifdef HAVE_PTHREAD
  pthread_mutex_lock(&this->lock);
#endif
  ntotal = this->ntotal;
  nbeyond = this->nbeyond;
#ifdef HAVE_PTHREAD
  pthread_mutex_unlock(&this->lock);
#endif

  while (noutput + nbeyond < ntotal) {	/* Previously check against this->ntotal */
#ifdef HAVE_PTHREAD
    pthread_mutex_lock(&this->lock);
    while (this->head == NULL && noutput + this->nbeyond < this->ntotal) {
      debug(fprintf(stderr,"__outbuffer_thread_anyorder waiting for filestring_avail_p\n"));
      pthread_cond_wait(&this->filestring_avail_p,&this->lock);
    }
    debug(fprintf(stderr,"__outbuffer_thread_anyorder woke up\n"));
#endif

    if (this->head == NULL) {
      /* False wake up */
#ifdef HAVE_PTHREAD
      pthread_mutex_unlock(&this->lock);
#endif

    } else {
#ifdef GSNAP
      this->head = RRlist_pop(this->head,&fp,&fp_failedinput_1,&fp_failedinput_2);
#else
      this->head = RRlist_pop(this->head,&fp,&fp_failedinput);
#endif
      debug1(RRlist_dump(this->head,this->tail));

#ifdef HAVE_PTHREAD
      /* Let worker threads put filestrings while we print */
      pthread_mutex_unlock(&this->lock);
#endif

#ifdef GSNAP
      Outbuffer_print_filestrings(fp,fp_failedinput_1,fp_failedinput_2);
#else
      Outbuffer_print_filestrings(fp,fp_failedinput);
#endif
      noutput += 1;
      /* Result_free(&result); */
      /* Request_free(&request); */

#ifdef HAVE_PTHREAD
      pthread_mutex_lock(&this->lock);
#endif
      if (this->head && this->nprocessed - noutput > output_buffer_size) {
	/* Clear out backlog */
	while (this->head && this->nprocessed - noutput > output_buffer_size) {
#ifdef GSNAP
	  this->head = RRlist_pop(this->head,&fp,&fp_failedinput_1,&fp_failedinput_2);
#else
	  this->head = RRlist_pop(this->head,&fp,&fp_failedinput);
#endif
	  debug1(RRlist_dump(this->head,this->tail));

#ifdef GSNAP
	  Outbuffer_print_filestrings(fp,fp_failedinput_1,fp_failedinput_2);
#else
	  Outbuffer_print_filestrings(fp,fp_failedinput);
#endif
	  noutput += 1;
	  /* Result_free(&result); */
	  /* Request_free(&request); */
	}
      }
#ifdef HAVE_PTHREAD
      pthread_mutex_unlock(&this->lock);
#endif
    }

    debug(fprintf(stderr,"__outbuffer_thread_anyorder has noutput %d, nbeyond %d, ntotal %d\n",
		  noutput,nbeyond,ntotal));

    /* Obtain this->ntotal while locked, to prevent race between output thread and input thread */
#ifdef HAVE_PTHREAD
    pthread_mutex_lock(&this->lock);
#endif
    ntotal = this->ntotal;
    nbeyond = this->nbeyond;
#ifdef HAVE_PTHREAD
    pthread_mutex_unlock(&this->lock);
#endif
  }

  assert(this->head == NULL);

  return (void *) NULL;
}



void *
Outbuffer_thread_ordered (void *data) {
  T this = (T) data;
  unsigned int output_buffer_size = this->output_buffer_size;
  unsigned int noutput = 0, nqueued = 0, ntotal, nbeyond;
  Filestring_T fp;
#ifdef GSNAP
  Filestring_T fp_failedinput_1, fp_failedinput_2;
#else
  Filestring_T fp_failedinput;
#endif
  RRlist_T queue = NULL;
  int id;

  /* Obtain this->ntotal while locked, to prevent race between output thread and input thread */
#ifdef HAVE_PTHREAD
  pthread_mutex_lock(&this->lock);
#endif
  ntotal = this->ntotal;
  nbeyond = this->nbeyond;
#ifdef HAVE_PTHREAD
  pthread_mutex_unlock(&this->lock);
#endif

  while (noutput + nbeyond < ntotal) {	/* Previously checked against this->ntotal */
#ifdef HAVE_PTHREAD
    pthread_mutex_lock(&this->lock);
    while (this->head == NULL && noutput + this->nbeyond < this->ntotal) {
      pthread_cond_wait(&this->filestring_avail_p,&this->lock);
    }
    debug(fprintf(stderr,"__outbuffer_thread_ordered woke up\n"));
#endif

    if (this->head == NULL) {
#ifdef HAVE_PTHREAD
      /* False wake up, or signal from worker_mpi_process */
      ntotal = this->ntotal;
      nbeyond = this->nbeyond;
      pthread_mutex_unlock(&this->lock);
#endif

    } else {
#ifdef GSNAP
      this->head = RRlist_pop(this->head,&fp,&fp_failedinput_1,&fp_failedinput_2);
#else
      this->head = RRlist_pop(this->head,&fp,&fp_failedinput);
#endif

#ifdef HAVE_PTHREAD
      /* Allow workers access to the queue */
      pthread_mutex_unlock(&this->lock);
#endif
      if ((id = Filestring_id(fp)) != (int) noutput) {
	/* Store in queue */
#ifdef GSNAP
	queue = RRlist_insert(queue,id,fp,fp_failedinput_1,fp_failedinput_2);
#else
	queue = RRlist_insert(queue,id,fp,fp_failedinput);
#endif
	nqueued++;
      } else {
#ifdef GSNAP
	Outbuffer_print_filestrings(fp,fp_failedinput_1,fp_failedinput_2);
#else
	Outbuffer_print_filestrings(fp,fp_failedinput);
#endif
	noutput += 1;

	/* Result_free(&result); */
	/* Request_free(&request); */
	
	/* Print out rest of stored queue */
	while (queue != NULL && queue->id == (int) noutput) {
#ifdef GSNAP
	  queue = RRlist_pop_id(queue,&id,&fp,&fp_failedinput_1,&fp_failedinput_2);
#else
	  queue = RRlist_pop_id(queue,&id,&fp,&fp_failedinput);
#endif
	  nqueued--;
#ifdef GSNAP
	  Outbuffer_print_filestrings(fp,fp_failedinput_1,fp_failedinput_2);
#else
	  Outbuffer_print_filestrings(fp,fp_failedinput);
#endif
	  noutput += 1;

	  /* Result_free(&result); */
	  /* Request_free(&request); */
	}
      }

#ifdef HAVE_PTHREAD
      pthread_mutex_lock(&this->lock);
#endif
      if (this->head && this->nprocessed - nqueued - noutput > output_buffer_size) {
	/* Clear out backlog */
	while (this->head && this->nprocessed - nqueued - noutput > output_buffer_size) {
#ifdef GSNAP
	  this->head = RRlist_pop(this->head,&fp,&fp_failedinput_1,&fp_failedinput_2);
#else
	  this->head = RRlist_pop(this->head,&fp,&fp_failedinput);
#endif
	  if ((id = Filestring_id(fp)) != (int) noutput) {
	    /* Store in queue */
#ifdef GSNAP
	    queue = RRlist_insert(queue,id,fp,fp_failedinput_1,fp_failedinput_2);
#else
	    queue = RRlist_insert(queue,id,fp,fp_failedinput);
#endif
	    nqueued++;
	  } else {
#ifdef GSNAP
	    Outbuffer_print_filestrings(fp,fp_failedinput_1,fp_failedinput_2);
#else
	    Outbuffer_print_filestrings(fp,fp_failedinput);
#endif
	    noutput += 1;
	    /* Result_free(&result); */
	    /* Request_free(&request); */
	
	    /* Print out rest of stored queue */
	    while (queue != NULL && queue->id == (int) noutput) {
#ifdef GSNAP
	      queue = RRlist_pop_id(queue,&id,&fp,&fp_failedinput_1,&fp_failedinput_2);
#else
	      queue = RRlist_pop_id(queue,&id,&fp,&fp_failedinput);
#endif
	      nqueued--;
#ifdef GSNAP
	      Outbuffer_print_filestrings(fp,fp_failedinput_1,fp_failedinput_2);
#else
	      Outbuffer_print_filestrings(fp,fp_failedinput);
#endif
	      noutput += 1;
	      /* Result_free(&result); */
	      /* Request_free(&request); */
	    }
	  }
	}
      }

#ifdef HAVE_PTHREAD
      pthread_mutex_unlock(&this->lock);
#endif
    }

    debug(fprintf(stderr,"__outbuffer_thread_ordered has noutput %d, nbeyond %d, ntotal %d\n",
		  noutput,nbeyond,ntotal));

    /* Obtain this->ntotal while locked, to prevent race between output thread and input thread */
#ifdef HAVE_PTHREAD
    pthread_mutex_lock(&this->lock);
#endif
    ntotal = this->ntotal;
    nbeyond = this->nbeyond;
#ifdef HAVE_PTHREAD
    pthread_mutex_unlock(&this->lock);
#endif
  }

  assert(queue == NULL);

  return (void *) NULL;
}

