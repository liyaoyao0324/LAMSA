/*
 * frag_check.c
 *
 * function: transform raw frag.msg to detailed frag.msg
 * method:   extend-ssw
 *
 * Created by Yan Gao on 08/29/2013
 */

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <time.h>
#include "frag_check.h"
#include "bntseq.h"
#include "ssw.h"
#include "kseq.h"
#include "ksw.h"

#define PER_LEN 100

KSEQ_INIT(gzFile, gzread)


/*
 * parameter: frag.msg
 */

int extend_ssw(int8_t* ref_seq, int8_t* read_seq, int ref_len, int read_len, int* ref_l_os, int* read_l_os, int* ref_r_os, int* read_r_os);

frag_msg* frag_init_msg(int frag_max)
{
	frag_msg *f_msg = (frag_msg*)malloc(sizeof(frag_msg));

	//XXX set frag_max as a constant value, then double
	f_msg->frag_max = frag_max; 
	f_msg->frag_num = 0;
	f_msg->last_len = PER_LEN;	//XXX
	f_msg->per_seed_max = 500;		//XXX
	f_msg->seed_num = 0;

	f_msg->fa_msg = (frag_aln_msg*)malloc(frag_max*sizeof(frag_aln_msg));
	int i;
	for (i = 0; i < frag_max; i++)
	{
		f_msg->fa_msg[i].cigar_max = 100;
		f_msg->fa_msg[i].cigar = (uint32_t*)malloc(100 * sizeof(uint32_t));
		f_msg->fa_msg[i].cigar_len = 0;

		f_msg->fa_msg[i].seed_i = (int*)malloc(f_msg->per_seed_max*sizeof(int));
		f_msg->fa_msg[i].seed_aln_i = (int*)malloc(f_msg->per_seed_max*sizeof(int));
		f_msg->fa_msg[i].seed_num = 0;
	}

	return f_msg;
}

void frag_free_msg(frag_msg *f_msg)
{
	int i;
	for (i = 0; i < f_msg->frag_max; i++)
	{
		free(f_msg->fa_msg[i].cigar);
		free(f_msg->fa_msg[i].seed_i);
		free(f_msg->fa_msg[i].seed_aln_i);
	}
	free(f_msg->fa_msg);
	free(f_msg);
}

int frag_set_msg(aln_msg *a_msg, int seed_i, int aln_i, int FLAG, frag_msg *f_msg, int frag_i, int seed_len)//FLAG 0:start / 1:end / 2:seed
{
	if (frag_i == 0)
		f_msg->seed_num=0;
	if (FLAG == 1)	//end
	{
		f_msg->fa_msg[frag_i].chr = a_msg[seed_i].at[aln_i].chr;
		f_msg->fa_msg[frag_i].srand = a_msg[seed_i].at[aln_i].nsrand;	//+:1/-:-1
		f_msg->fa_msg[frag_i].seed_i[0] = seed_i;
		f_msg->fa_msg[frag_i].seed_aln_i[0] = aln_i;
		f_msg->fa_msg[frag_i].seed_num = 1;
		f_msg->seed_num++;
		f_msg->fa_msg[frag_i].flag = UNCOVERED;
		if (f_msg->fa_msg[frag_i].srand == 1)	//+ srand
			f_msg->fa_msg[frag_i].ex_ref_end = f_msg->fa_msg[frag_i].ref_end = a_msg[seed_i].at[aln_i].offset + seed_len - 1;
		else							//- srand
			f_msg->fa_msg[frag_i].ex_ref_end = f_msg->fa_msg[frag_i].ref_end = a_msg[seed_i].at[aln_i].offset;
		f_msg->fa_msg[frag_i].ex_read_end = f_msg->fa_msg[frag_i].read_end = 2*seed_len*(a_msg[seed_i].read_id-1)+seed_len;
	}
	else if(FLAG==0)	//start
	{
		f_msg->frag_num = frag_i + 1;
		f_msg->fa_msg[frag_i].ex_read_begin = f_msg->fa_msg[frag_i].read_begin = 2*seed_len*(a_msg[seed_i].read_id-1)+1;
		if (f_msg->fa_msg[frag_i].srand == 1)	//+ srand
			f_msg->fa_msg[frag_i].ex_ref_begin = f_msg->fa_msg[frag_i].ref_begin = a_msg[seed_i].at[aln_i].offset;
		else							//- srand
			f_msg->fa_msg[frag_i].ex_ref_begin = f_msg->fa_msg[frag_i].ref_begin = a_msg[seed_i].at[aln_i].offset + seed_len-1;
		f_msg->fa_msg[frag_i].per_n = (f_msg->fa_msg[frag_i].read_end - seed_len - f_msg->fa_msg[frag_i].read_begin+1) / 2 / seed_len + 1;
	}
	else			//seed
	{
		f_msg->fa_msg[frag_i].seed_i[f_msg->fa_msg[frag_i].seed_num] = seed_i;
		f_msg->fa_msg[frag_i].seed_aln_i[f_msg->fa_msg[frag_i].seed_num] = aln_i;
		f_msg->seed_num++;
		f_msg->fa_msg[frag_i].seed_num++;
	}
	return 0;
}

int frag_refresh(frag_msg* f_msg, int f_i, int ref_offset, int read_offset, int FLAG)
{
	if (FLAG == RIGHT)	//right offset
	{
		f_msg->fa_msg[f_i].ex_read_end += read_offset;
		if (f_msg->fa_msg[f_i].srand == 1)	//+ srand
			f_msg->fa_msg[f_i].ex_ref_end += ref_offset;
		else						//- srand
			f_msg->fa_msg[f_i].ex_ref_end -= ref_offset;
//		f_msg->fa_msg[f_i].per_n += read_offset / PER_LEN;
	}
	else				//left offset
	{
		f_msg->fa_msg[f_i].ex_read_begin -= read_offset;
		if (f_msg->fa_msg[f_i].srand == 1)	//+ srand
			f_msg->fa_msg[f_i].ex_ref_begin -= ref_offset;
		else
			f_msg->fa_msg[f_i].ex_ref_begin += ref_offset;
//		f_msg->fa_msg[f_i].per_n += read_offset / PER_LEN;
	}
	//printf("Refresh: %d %d %d %d\n", f_i, ref_offset, read_offset, FLAG);
	return 0;
}

int frag_covered(frag_msg *f_msg, int f_i, int b_f)
{
	f_msg->fa_msg[b_f].flag = COVERED;
	f_msg->fa_msg[f_i].b_f = b_f;
	//	printf("Covered: %d C %d\n", f_i, b_f);
	return 0;
}

/*
 * check long OR short and set ref_len/read_len
 * frag : end   -> return 2
 * long : short -> return 1
 * short : short |
 * long : long  -> return 0
 * short : long -> return -1
 * head : frag  -> return -2
 */
/*
int ls_frag(frag_msg* f_msg, int f_i, int* ref_len, int* read_len)
{
	if (f_i == f_msg->frag_num)	//check if there is un-mapped region before the first frag
	{
		if (f_msg->read_begin[f_msg->c_i[f_i-1]] != 1)	//Bingo!
		{
			*ref_len = f_msg->read_begin[f_msg->c_i[f_i-1]] - 1;
			*read_len = *ref_len;
			return -2;
		}
		return -3;
	}
	else if (f_i == 0)	//last frag
	{
		(*read_len) = f_msg->last_len;
		(*ref_len) = *read_len;
		return 2;
	}

	(*read_len) = f_msg->read_begin[f_msg->c_i[f_i-1]] - f_msg->read_end[f_msg->c_i[f_i]] - 1;
	(*ref_len) = (*read_len);
	
	int ret = 0;

	ret+=(f_msg->per_n[f_msg->c_i[f_i]] < 5 ?-1:0);
	ret+=(f_msg->per_n[f_msg->c_i[f_i-1]] < 5 ?1:0);

	return ret;
}*/

int unsoap2dp_extend(frag_msg *f_msg, int f_i, int FLAG, bntseq_t *bns, int8_t *pac, char *read_seq, int8_t *seq1, int8_t *seq2)
{
	int ret;
	int ref_len, read_len;
	int ref_l_os, read_l_os, ref_r_os, read_r_os;
	int b_f = f_msg->fa_msg[f_i].b_f;
	int ff, m;
	char *seq_s2 = malloc(16385*sizeof(char));

	// XXX Is it necessary to take a 'COVERED frag' as an individaul frag in the next reverse extend?
	//	   Here is 'IS'.
	
	if (FLAG == RIGHT)
	{
		if (b_f == 0)
		{
			read_len = f_msg->last_len;
			ref_len = read_len;
		}
		else
		{
			read_len = f_msg->fa_msg[b_f-1].read_begin - f_msg->fa_msg[b_f].read_end - 1;
			ref_len = read_len;
		}
		//XXX pac2fa: left -> OR right ->
		if (f_msg->fa_msg[b_f].srand == -1)	//- srand
			pac2fa_core(bns, pac, f_msg->fa_msg[b_f].chr, f_msg->fa_msg[b_f].ref_end-1-ref_len, &ref_len, f_msg->fa_msg[b_f].srand, &ff, seq1);
		else
			pac2fa_core(bns, pac, f_msg->fa_msg[b_f].chr, f_msg->fa_msg[b_f].ref_end, &ref_len, f_msg->fa_msg[b_f].srand, &ff, seq1);
		strncpy(seq_s2, read_seq+f_msg->fa_msg[b_f].read_end, read_len);
		for (m = 0; m < read_len; ++m) seq2[m] = nst_nt4_table[(int)seq_s2[m]];
		ret = extend_ssw(seq1, seq2, ref_len, read_len, &ref_l_os, &read_l_os, &ref_r_os, &read_r_os);
		frag_refresh(f_msg, f_i, ref_r_os, read_r_os, RIGHT);
	}
	else	//LEFT
	{
		if (b_f == f_msg->frag_num - 1)
		{
			if (f_msg->fa_msg[b_f].read_begin != 1)	//Un-soap2dp HEAD
			{
				read_len = f_msg->fa_msg[b_f].read_begin - 1;
				ref_len =read_len;
			}
			else
				return BAD_ALIGN;
		}
		else
		{
			read_len = f_msg->fa_msg[b_f].read_begin - f_msg->fa_msg[b_f+1].read_end - 1;
			ref_len = read_len;
		}
		if (f_msg->fa_msg[b_f].srand == -1)
			pac2fa_core(bns, pac, f_msg->fa_msg[b_f].chr, f_msg->fa_msg[b_f].ref_begin, &ref_len, f_msg->fa_msg[b_f].srand, &ff, seq1);
		else
			pac2fa_core(bns, pac, f_msg->fa_msg[b_f].chr, f_msg->fa_msg[b_f].ref_begin-ref_len-1, &ref_len, f_msg->fa_msg[b_f].srand, &ff, seq1);
		strncpy(seq_s2, read_seq+(f_msg->fa_msg[b_f].read_begin - read_len -1), read_len);
		for (m = 0; m < read_len; ++m) seq2[m] = nst_nt4_table[(int)seq_s2[m]];
		ret = extend_ssw(seq1, seq2, ref_len, read_len, &ref_l_os, &read_l_os, &ref_r_os, &read_r_os);
		frag_refresh(f_msg, f_i, ref_l_os, read_l_os, LEFT);
	}
	return ret;
}

//Extend the frag around with f_i
//Extend PER_LEN in a while cycle, stop when get a BAD_ALIGN
//XXX If it's SHORT, extend the whole frag
//XXX Else, extend PER_LEN in a while cycle, stop when get a BAD_ALIGN
/*
int frag_extend(frag_msg *f_msg, int f_i, int FLAG, bntseq_t *bns, int8_t *pac, char *read_seq, int8_t *seq1, int8_t *seq2, int seed_len)
{
	int ret, i;
	int ref_len, read_len;
	int ref_l_os, read_l_os, ref_r_os, read_r_os;
	int b_f = f_msg->fa_msg[f_i].b_f;
	int ff, m;
	char *seq_s2 = malloc(16385*sizeof(char));
	if (FLAG == RIGHT)
	{
		if (b_f == 0)
		{
			//printf("BAD\n");
			return BAD_ALIGN;
		}
		ref_len = read_len = seed_len;
		for (i = 0; i < f_msg->fa_msg[b_f-1].per_n; i++)
		{
			if (f_msg->fa_msg[f_i].srand == -1)
				pac2fa_core(bns, pac, f_msg->fa_msg[f_i].chr, f_msg->fa_msg[f_i].ex_ref_end-1-ref_len, &ref_len, f_msg->fa_msg[f_i].srand, &ff, seq1);
			else
				pac2fa_core(bns, pac, f_msg->fa_msg[f_i].chr, f_msg->fa_msg[f_i].ex_ref_end, &ref_len, f_msg->fa_msg[f_i].srand, &ff, seq1);
			strncpy(seq_s2, read_seq + (f_msg->fa_msg[b_f-1].read_begin - 1 + i * seed_len), read_len);
			for (m = 0; m < read_len; ++m) seq2[m] = nst_nt4_table[(int)seq_s2[m]];
			ret = extend_ssw(seq1, seq2, ref_len, read_len, &ref_l_os, &read_l_os, &ref_r_os, &read_r_os);
			frag_refresh(f_msg, f_i, ref_r_os, read_r_os, RIGHT);
			if (ret == BAD_ALIGN)
				return BAD_ALIGN;
		}
		frag_covered(f_msg, f_i, b_f-1);
	}
	else	//LEFT
	{
		if (b_f == f_msg->frag_num-1)
			return BAD_ALIGN;
		ref_len = read_len = seed_len;
		for (i = 1; i <= f_msg->fa_msg[b_f+1].per_n; i++)
		{
			if (f_msg->fa_msg[f_i].srand == -1)
				pac2fa_core(bns, pac, f_msg->fa_msg[f_i].chr, f_msg->fa_msg[f_i].ex_ref_begin, &ref_len, f_msg->fa_msg[f_i].srand, &ff, seq1);
			else
				pac2fa_core(bns, pac, f_msg->fa_msg[f_i].chr, f_msg->fa_msg[f_i].ex_ref_begin-1-ref_len, &ref_len, f_msg->fa_msg[f_i].srand, &ff, seq1);
			strncpy(seq_s2, read_seq + (f_msg->fa_msg[b_f+1].read_end - i * seed_len), read_len);
			for (m = 0; m < read_len; ++m) seq2[m] = nst_nt4_table[(int)seq_s2[m]];
			ret = extend_ssw(seq1, seq2, ref_len, read_len, &ref_l_os, &read_l_os, &ref_r_os, &read_r_os);

			frag_refresh(f_msg, f_i, ref_l_os, read_l_os, LEFT);
			if (ret == BAD_ALIGN)
				return BAD_ALIGN;
		}
		frag_covered(f_msg, f_i, b_f+1);
	}
	return GOOD_ALIGN;
}
*/

//XXX combine getintv1 & 2
int getintv1(int8_t *seq1, bntseq_t *bns, int8_t *pac, aln_msg *a_msg, int seed1_i, int seed1_aln_i, int seed2_i, int seed2_aln_i, int seed_len)
{
	int64_t start;
	int32_t len;
	int flag;
	if (a_msg[seed1_i].at[seed1_aln_i].nsrand == 1)	//'+' srand
	{
		start = a_msg[seed1_i].at[seed1_aln_i].offset + seed_len - 1 + a_msg[seed1_i].at[seed1_aln_i].len_dif;
		len = a_msg[seed2_i].at[seed2_aln_i].offset - 1 - start;
		pac2fa_core(bns, pac, a_msg[seed1_i].at[seed1_aln_i].chr, start, &len, 1, &flag, seq1);
	}
	else	//'-' srand
	{
		start = a_msg[seed2_i].at[seed2_aln_i].offset + seed_len - 1 + a_msg[seed2_i].at[seed2_aln_i].len_dif;
		len = a_msg[seed1_i].at[seed1_aln_i].offset - 1 - start;
		pac2fa_core(bns, pac, a_msg[seed1_i].at[seed1_aln_i].chr, start, &len, -1, &flag, seq1);
	}
	int j;
	fprintf(stdout, "ref:\t%d\t%ld\t", len, start);
	for (j = 0; j < len; j++)
		fprintf(stdout, "%d", (int)seq1[j]);
	fprintf(stdout, "\n");
	return (int)len;
}

int getintv2(int8_t *seq2, char *read_seq, aln_msg *a_msg, int seed1_i, int seed1_aln_i, int seed2_i, int seed2_aln_i, int *band_width, int seed_len)
{
	int32_t i, s;
	int j;

	//set band-width
	*band_width = 2 * (((seed2_i - seed1_i) << 1) - 1) * MAXOFTWO(a_msg[seed1_i].at[seed1_aln_i].bmax, a_msg[seed2_i].at[seed2_aln_i].bmax);

	//convert char seq to int seq
	for (j=0, s = i = a_msg[seed1_i].read_id * 2 * seed_len - seed_len; i < (a_msg[seed2_i].read_id-1) * 2 * seed_len; ++j, ++i)
		seq2[j] = nst_nt4_table[(int)read_seq[i]];
	fprintf(stdout, "read:\t%d\t%d\t", j, s);
	for (i = 0; i < j; i++)
		fprintf(stdout, "%d",(int)seq2[i]);
	fprintf(stdout, "\n");
	return (j);
}

//merge interval and seed to frag
//Before, frag has the first seed's msg already
void merge_cigar(int cigar_len, uint32_t *cigar, frag_msg *f_msg, int f_i)
{
	int fc_len = f_msg->fa_msg[f_i].cigar_len;
	uint32_t *fcigar = f_msg->fa_msg[f_i].cigar;

	f_msg->fa_msg[f_i].cigar_len cigar
	if (fcigar[fc_len-1] & 0xf != CMATCH || cigar[0] & 0xf != CMATCH)	//bound-repair
	{
		    /* seq1, ref */ /* seq2, read */
		int len1, len11=0,  len2, len21=0, len22=0, len_dif=0;
		uint8_t *seq1, *seq2;
		int64_t ref_start;
		int32_t read_start;

		if (fcigar[fc_len-1] & 0xf == CMATCH)
			len21 += MINOFTWO(3, (int)(fcigar[fc_len-1] >> 4));
		else
		{
			if (fcigar[fc_len-1] & 0xf == CINS)	{
				len21 += (int)(fcigar[fc_len-1]>>4);
				len_dif -= len21;
			} else {fprintf(stderr, "cigar [D] error.\n");exit(-1);}
			if (fcigar[fc_len-2] & 0xf != CMATCH) {fprintf(stderr, "cigar error.\n");exit(-1);}
			len21 += MINOFTWO(3, (int)(fcigar[fc_len-2] >> 4));	
		}
		len11 = len21 + len_dif;
		read_start = f_msg->fa_msg[f_i].cigar_read_end - len21 + 1;	//1-based
		ref_start = f_msg->fa_msg[f_i].cigar_ref_end - len11 + 1;	//1-based

		if (cigar[0] & 0xf == CMATCH)
			len22 += MINOFTWO(3, (int)(cigar[0] >> 4));
		else
		{
			if (cigar[0] & 0xf == CINS) {
				len22 += (int)(cigar[0]>>4);
				len_dif -= len22;
			} else 	//CDEL
				len_dif += (int)(cigar[0]>>4);
			if (cigar[1] & 0xf != CMATCH) {fprintf(stderr, "cigar error.\n");exit(-1);}
			len22 += MINOFTWO(3, (int)(cigar[1] >> 4));
		}
		len2 = len21 + len22;
		len1 = len2 + len_dif;
	}
}
//Extend the intervals between seeds in the same frag
int frag_extend(frag_msg *f_msg, aln_msg *a_msg, int f_i, bntseq_t *bns, int8_t *pac, char *read_seq, int8_t *seq1, int8_t *seq2, int seed_len)
{
	int i, j, seed_i, seed_aln_i, last_i, last_aln_i;
	int len1, len2;
	int b, cigar_len, score;
	uint32_t *cigar=0;

	int8_t mat[25] = {1, -2, -2, -2, -1,
					 -2,  1, -2, -2, -1,
					 -2, -2,  1, -2, -1,
					 -2, -2, -2,  1, -1,
					 -1, -1, -1, -1, -1};
	//banded global alignment for intervals between seeds
	cigar_len = 0;
	//get the first node
	i = f_msg->fa_msg[f_i].seed_num-1;
	last_i = f_msg->fa_msg[f_i].seed_i[i];
	last_aln_i = f_msg->fa_msg[f_i].seed_aln_i[i];
	//copy the first seed's cigar to frag
	{
		f_msg->fa_msg[f_i].cigar_len = a_msg[last_i].at[last_aln_i].cigar_len;
		for (j = 0; j < f_msg->fa_msg[f_i].cigar_len; j++)
			f_msg->fa_msg[f_i].cigar[j] = a_msg[last_i].at[last_aln_i].cigar[j];
		f_msg->fa_msg[f_i].cigar_ref_start = a_msg[last_i].at[last_aln_i].offset;				//1-based
		f_msg->fa_msg[f_i].cigra_read_start = (a_msg[last_i].read_id - 1) *2*seed_len+1;		//1-based
		f_msg->fa_msg[f_i].cigar_read_end = f_msg->fa_msg[f_i].cigar_read_start - 1 + seed_len;	//1-based
		f_msg->fa_msg[f_i].len_dif = a_msg[last_i].at[last_aln_i].len_dif;
		f_msg->fa_msg[f_i].cigar_ref_end = f_msg->fa_msg[f_i].ref_start-1+seed_len + len_dif;	//1-based
		f_msg->fa_msg[f_i].edit_dis = a_msg[last_i].at[last_aln_i].edit_dis;
	}
	for (--i; i >= 0; --i)
	{
		seed_i = f_msg->fa_msg[f_i].seed_i[i];
		seed_aln_i = f_msg->fa_msg[f_i].seed_aln_i[i];

		//get ref seq and seed-interval seq
		//	fprintf(stdout, "pre:\t");
		//	for (j = 0; j < a_msg[last_i].at[last_aln_i].cigar_len; j++)
		//		fprintf(stdout, "%d%c", (a_msg[last_i].at[last_aln_i].cigar[j])>>4, "MIDNHS"[a_msg[last_i].at[last_aln_i].cigar[j]&0xf]);
		//	fprintf(stdout, "\n");
		len1 = getintv1(seq1, bns, pac, a_msg, last_i, last_aln_i, seed_i, seed_aln_i, seed_len);
		len2 = getintv2(seq2, read_seq, a_msg, last_i, last_aln_i, seed_i, seed_aln_i, &b, seed_len);
		cigar_len = 0;
		score = ksw_global(len2, seq2, len1, seq1, 5, &mat, 2, 1, b, &cigar_len, &cigar);
		for (j = 0; j < cigar_len; j++)
			fprintf(stdout, "%d%c", (cigar[j])>>4, "MIDNHS"[cigar[j]&0xf]);
		printf("\t%d\n", b);
		merge_cigar(cigar_len, cigar, f_msg, f_i);	//merge interval to frag
		merge_cigar(a_msg[seed_i].at[seed_aln_i].cigar_len, a_msg[seed_i].at[seed_aln_i].cigar, f_msg, f_i);	//merge seed to frag
		last_i = seed_i;
		last_aln_i = seed_aln_i;
	}
	free(cigar);
	return 0;
}

int frag_check(bntseq_t *bns, int8_t *pac, char *read_prefix, char *read_seq, frag_msg *f_msg, aln_msg *a_msg, int seed_len)
{
	int i;
	fprintf(stdout, "frag:\n");
	for (i = f_msg->frag_num-1; i>= 0; i--)
	{
		if (f_msg->fa_msg[i].flag == COVERED)
			fprintf(stdout, "COVERED ");
		fprintf(stdout, "ref %d %d %d read %d %d per_n %d\n", f_msg->fa_msg[i].chr, f_msg->fa_msg[i].ref_begin, f_msg->fa_msg[i].ref_end, f_msg->fa_msg[i].read_begin, f_msg->fa_msg[i].read_end, f_msg->fa_msg[i].per_n);
	}

	int max_len = 16384 ;
	int8_t *seq1 = (int8_t*)malloc((max_len+1)*sizeof(int8_t));
	int8_t *seq2 = (int8_t*)malloc((max_len+1)*sizeof(int8_t));
	/*
	 * extend once
	 * for every frag : take it as a RIGHT frag
	 * for every interval : SV breakpoint(s).
	 */ 
	for (i = f_msg->frag_num-1; i > 0; i--)
	{
		frag_extend(f_msg, a_msg, i, bns, pac, read_seq, seq1, seq2, seed_len);
//		split_mapping(f_msg, a_msg, i, i-1);	
	}
	frag_extend(f_msg, a_msg, i, bns, pac, read_seq, seq1, seq2, seed_len);
/*
	for (i = f_msg->frag_num-1; i >= 0; i--)
	{
		if (f_msg->flag[i] == UNCOVERED)
			fprintf(stdout, "ref: %d %d  read: %d %d\n", f_msg->ex_ref_begin[i], f_msg->ex_ref_end[i], f_msg->ex_read_begin[i], f_msg->ex_read_end[i]);
	}*/
	free(seq1);
	free(seq2);
	return 0;
}
