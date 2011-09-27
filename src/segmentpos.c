static char rcsid[] = "$Id: segmentpos.c,v 1.52 2005/03/11 17:56:19 twu Exp $";
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "segmentpos.h"
#include <stdio.h>
#include "mem.h"
#include "intlist.h"
#include "separator.h"


#ifdef DEBUG
#define debug(x) x
#else
#define debug(x)
#endif


#define MAXACCESSIONS 10


#define T Segmentpos_T
struct T {
  Chrom_T chrom;		/* 4 bytes */
  Genomicpos_T chrpos1;		/* 4 bytes */
  Genomicpos_T chrpos2;		/* 4 bytes */
  Genomicpos_T length;		/* 4 bytes */
  int type;			/* 4 bytes */
  bool revcompp;
};


Chrom_T
Segmentpos_chrom (T this) {
  return this->chrom;
}

Genomicpos_T
Segmentpos_chrpos1 (T this) {
  return this->chrpos1;
}

Genomicpos_T
Segmentpos_chrpos2 (T this) {
  return this->chrpos2;
}

Genomicpos_T
Segmentpos_length (T this) {
  return this->length;
}

int
Segmentpos_type (T this) {
  return this->type;
}

bool
Segmentpos_revcompp (T this) {
  return this->revcompp;
}


T
Segmentpos_new (Chrom_T chrom, Genomicpos_T chrpos1, Genomicpos_T chrpos2, 
		bool revcompp, Genomicpos_T length, int type) {
  T new = (T) MALLOC(sizeof(*new));

  new->chrom = chrom;
  new->chrpos1 = chrpos1;
  new->chrpos2 = chrpos2;
  new->revcompp = revcompp;
  new->length = length;
  new->type = type;
  return new;
}

void
Segmentpos_free (T *old) {
  Chrom_free(&(*old)->chrom);
  FREE(*old);
  return;
}

void
Segmentpos_print (FILE *fp, T this, char *acc, Genomicpos_T offset) {
  char *chrstring;

  chrstring = Chrom_to_string(this->chrom);
  fprintf(fp,"%s\t%u\t%s\t%u\t%u\n",acc,offset+this->chrpos1,chrstring,this->chrpos1,this->length);
  FREE(chrstring);
  return;
}

int
Segmentpos_compare (const void *x, const void *y) {
  T a = * (T *) x;
  T b = * (T *) y;
  int cmp;

  if ((cmp = Chrom_cmp(a->chrom,b->chrom)) != 0) {
    return cmp;
  } else if (a->chrpos1 < b->chrpos1) {
    return -1;
  } else if (b->chrpos1 < a->chrpos1) {
    return 1;
  } else {
    return 0;
  }
}

static bool
altstrain_sufficient_p (Intlist_T indices, IIT_T contig_iit, Genomicpos_T position1,
			Genomicpos_T position2, char *align_strain) {
  Intlist_T p;
  Genomicpos_T contig_start, contig_end, contig_length;
  int index, contig_straintype;
  Interval_T interval;

  p = indices;
  while (p != NULL) {
    index = Intlist_head(p);
    interval = IIT_interval(contig_iit,index);
    contig_straintype = Interval_type(interval);
    if (!strcmp(IIT_typestring(contig_iit,contig_straintype),align_strain)) {
      contig_start = Interval_low(interval);
      contig_length = Interval_length(interval);
      contig_end = contig_start + contig_length;

      if (contig_start <= position1 && contig_end >= position2) {
	debug(printf("Altstrain is sufficient: %u..%u subsumes %u..%u\n",
		     contig_start,contig_end,position1,position2));
	return true;
      }
    }
    p = Intlist_next(p);
  }

  debug(printf("Altstrain is not sufficient\n"));
  return false;
}


static bool
contig_print_p (IIT_T contig_iit, int contig_straintype, bool referencealignp,
		char *align_strain, bool printreferencep, bool printaltp) {
  if (contig_straintype == 0) {
    /* Contig is from reference strain */
    if (printreferencep == true) {
      return true;
    } else {
      return false;
    }

  } else if (referencealignp == true || align_strain == NULL) {
    /* Contig is from an alternate strain */
    if (printaltp == true) {
      return true;
    } else {
      return false;
    }

  } else if (strcmp(IIT_typestring(contig_iit,contig_straintype),align_strain)) {
    /* Contig is from a non-relevant alternate strain */
    return false;

  } else {
    /* Contig is from the aligned alternate strain */
    if (printaltp == true) {
      return true;
    } else {
      return false;
    }
  }
}


void
Segmentpos_print_accessions (IIT_T contig_iit, Genomicpos_T position1,
			     Genomicpos_T position2, bool referencealignp, 
                             char *align_strain, bool zerobasedp) {
  Genomicpos_T contig_start, contig_end, contig_length;
  int relstart, relend;		/* Need to be signed int, not long or unsigned long */
  int index, contig_straintype, i = 0;
  char *comma1, *comma2, *annotation, *contig_strain;
  Intlist_T indices, p;
  Interval_T interval;
  bool printreferencep, printaltp, firstprintp = false;

  printf("    Accessions: ");

  p = indices = IIT_get(contig_iit,position1,position2);
  if (referencealignp == true) {
    printreferencep = true;
    printaltp = false;
  } else if (altstrain_sufficient_p(indices,contig_iit,position1,position2,align_strain) == true) {
    printreferencep = false;
    printaltp = true;
  } else {
    printreferencep = true;
    printaltp = true;
  }

  while (p != NULL && i < MAXACCESSIONS) {
    index = Intlist_head(p);
    interval = IIT_interval(contig_iit,index);
    contig_straintype = Interval_type(interval);
    if (contig_print_p(contig_iit,contig_straintype,referencealignp,align_strain,
		       printreferencep,printaltp) == true) {
      contig_start = Interval_low(interval);
      contig_length = Interval_length(interval);

      relstart = position1 - contig_start;
      if (relstart < 0) {
	relstart = 0;
      }
      relend = position2 - contig_start;
      if (relend > contig_length) {
	relend = contig_length;
      }

      comma1 = Genomicpos_commafmt((Genomicpos_T) (relstart + !zerobasedp));
      comma2 = Genomicpos_commafmt((Genomicpos_T) (relend + !zerobasedp));
      annotation = IIT_annotation(contig_iit,index);

      if (firstprintp == true) {
	printf("; ");
      } else {
	firstprintp = true;
      }

      if (annotation[0] == '-') {
	printf("[-]");
      }
      printf("%s",IIT_label(contig_iit,index));
      if (referencealignp == false && contig_straintype == 0) {
	printf("[reference strain]");
      }
      printf(":%s%s%s (out of %d bp)",comma1,SEPARATOR,comma2,contig_length);

      FREE(comma2);
      FREE(comma1);

      i++;
    }
    p = Intlist_next(p);
  }
  printf("\n");

  Intlist_free(&indices);
  return;
}

