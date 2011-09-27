/* $Id: comp.h,v 1.1 2005/10/06 20:42:04 twu Exp $ */
#ifndef COMP_INCLUDED
#define COMP_INCLUDED

/* Non-gap */
#define MATCH_COMP '|'
#define DYNPROG_MATCH_COMP '*'
#define AMBIGUOUS_COMP ':'
#define MISMATCH_COMP ' '
#define INDEL_COMP '-'
#define SHORTGAP_COMP '~'	/* Prints as INDEL_COMP, but scores as NONINTRON_COMP */
#define DIAGNOSTIC_SHORTEXON_COMP 's'/* Used only at print time */

/* Gap */
#define FWD_CANONICAL_INTRON_COMP '>'
#define FWD_GCAG_INTRON_COMP ')'
#define FWD_ATAC_INTRON_COMP ']'
#define REV_CANONICAL_INTRON_COMP '<'
#define REV_GCAG_INTRON_COMP '('
#define REV_ATAC_INTRON_COMP '['

#define NONINTRON_COMP '='
#define DUALBREAK_COMP '#'

#define INTRONGAP_COMP '.'
#define INTRONGAP_CHAR '.'


#ifdef PMAP
#define BACKTRANSLATE_CHAR '?'
#endif



#endif