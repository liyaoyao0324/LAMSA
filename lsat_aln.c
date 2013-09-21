#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <zlib.h>
#include "lsat_aln.h"
#include "kseq.h"
KSEQ_INIT(gzFile, gzread)

char FLAG[4][10] = {"MATCH", "INSERT", "DELETION", "CHR_DIF"};

int price[SEED_INIT_MAX][PER_ALN_N];    //p[i] = max{p[i-1][j] + cost[j,k]}
path_msg path[SEED_INIT_MAX][PER_ALN_N];

int usage(void )		//aln usage
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage:   lsat aln [options] <ref_prefix> <in.fa/fq>\n\n");
	fprintf(stderr, "Options: -n              Do NOT excute soap2-dp program, when soap2-dp result existed already.\n");
	fprintf(stderr, "         -a [STR]        The soap2-dp alignment result. When '-n' is used. [Def=\"seed_prefix.out.0\"]\n");
	fprintf(stderr, "         -m [INT][STR]  The soap2-dp option. Maximun #errors allowed. [Def=3e]\n");
	fprintf(stderr, "         -l [INT]        The length of seed. [Def=100]\n");
	fprintf(stderr, "         -o [STR]        The output file (SAM format). [Def=\"prefix_out.sam\"]\n");

	fprintf(stderr, "\n");
	return 1;
}

seed_msg *seed_init_msg(void)
{
	seed_msg *msg = (seed_msg*)malloc(sizeof(seed_msg));

	msg->n_read = 0;
	msg->n_seed = (int *)calloc(READ_INIT_MAX, sizeof(int));
	msg->seed_max = 0;

	return msg;
}

void seed_free_msg(seed_msg *msg)
{
	free(msg->n_seed);
	free(msg);
}

int split_seed(char *prefix, seed_msg *s_msg, int seed_len)
{
	gzFile infp;
	kseq_t *seq;
	char out_f[1024], seed_head[1024], seed_seq[1024];
	FILE *outfp;
	int m_read, n_seed, *new_p, i;

	if ((infp = gzopen(prefix, "r")) == NULL)
	{
		fprintf(stderr, "[lsat_aln] Can't open %s\n", prefix);
		exit(-1);
	}
	seq = kseq_init(infp);

	strcpy(out_f, prefix); strcat(out_f, ".seed");
	if ((outfp = fopen(out_f, "w")) == NULL)
	{
		fprintf(stderr, "[lsat_aln] Can't open %s\n", out_f);
		exit(-1);
	}
	
	/*
		typedef struct {	//全部read包含seed数目信息
		int n_read; //获取的read总数目			XXX contig_all -> n_read
		int *n_seed;	//存放每条read的seed数目	XXX READ_NUM -> n_seed
		int seed_max;   //contig中分割成短read的数目最大值	//XXX read_max -> seed_max
		} seed_msg;
	 */
	seed_seq[seed_len] = '\n';
	m_read = READ_INIT_MAX;
	while (kseq_read(seq) >= 0)
	{
		n_seed = ((seq->seq.l / seed_len) + 1) >> 1;
		if (n_seed > s_msg->seed_max) s_msg->seed_max = n_seed;
		if (s_msg->n_read == m_read-1)
		{
			m_read <<= 1;
			if ((new_p = (int*)realloc(s_msg->n_seed, m_read * sizeof(int))) == NULL)
			{
				free(s_msg->n_seed);
				fprintf(stderr, "[lsat_aln] Can't allocate more memory.\n");
				exit(1);
			}
			s_msg->n_seed = new_p;
		}
		s_msg->n_read++;
		s_msg->n_seed[s_msg->n_read] = s_msg->n_seed[s_msg->n_read-1] + n_seed;
		for (i = 0; i < n_seed; i++)
		{
			sprintf(seed_head, ">%s_%d:%d\n", seq->name.s, i, i*seed_len*2);
			strncpy(seed_seq, seq->seq.s+i*seed_len*2, seed_len);
			seed_seq[seed_len+1] = '\0';
			fputs(seed_head, outfp);
			fputs(seed_seq, outfp);
		}
	}

	gzclose(infp);
	fclose(outfp);
	kseq_destroy(seq);
	
	return 0;
}

aln_msg *aln_init_msg(int seed_max)
{
	aln_msg *msg;
	int i;
	msg = (aln_msg*)malloc(seed_max * sizeof(aln_msg));
	for (i = 0; i < seed_max; i++)		//drop away seed whose number of alignments > PER_ALN_N
	{
		msg[i].read_id = -1;
    	msg[i].chr = (int32_t *)malloc(PER_ALN_N * sizeof(int32_t));		
    	msg[i].offset = (int32_t *)malloc(PER_ALN_N * sizeof(int32_t));
    	msg[i].nsrand = (int8_t *)malloc(PER_ALN_N * sizeof(int8_t));
    	msg[i].edit_dis = (int8_t *)malloc(PER_ALN_N * sizeof(int8_t));
    	msg[i].n_aln = 0;
	}
	return msg;
}

void aln_free_msg(aln_msg *a_msg, int seed_max)	//a_msg大小为read_max个
{
	int i;
	for (i = 0; i < seed_max; i++)
	{
    	free(a_msg[i].chr);
    	free(a_msg[i].offset);
    	free(a_msg[i].nsrand);
    	free(a_msg[i].edit_dis);
	}
	free(a_msg);
}

void setAmsg(aln_msg *a_msg, int32_t read_x, int aln_y, int32_t read_id, int32_t chr, int32_t offset, int8_t srand, int8_t edit_dis)
{   //read_x: (除去unmap和repeat的)read序号, aln_y: read对应的比对结果序号(从1开始)
	if (aln_y > PER_ALN_N)
	{
		fprintf(stderr, "setAmsg ERROR!\n");
		exit(0);
	}
	a_msg[read_x-1].read_id = read_id;
	a_msg[read_x-1].chr[aln_y-1] = chr;
	a_msg[read_x-1].offset[aln_y-1] = offset;
	a_msg[read_x-1].nsrand[aln_y-1] = ((srand=='+')?1:-1);
	a_msg[read_x-1].edit_dis[aln_y-1] = edit_dis;
	a_msg[read_x-1].n_aln = aln_y;
}

int get_min_dis(aln_msg *a_msg, int i, int j, int k, int* flag)    //(i,j)对应节点，来自i-1的第k个aln
{
    if (i < 1)
    {
        printf("ERROR\n");
        exit(0);
    }
    if (a_msg[i].chr[j] != a_msg[i-1].chr[k] || a_msg[i].nsrand[j] != a_msg[i-1].nsrand[k])	//不同染色体
    {
        *flag = CHR_DIF;
    	return PRICE_DIF_CHR;
    }
    int exp = a_msg[i-1].offset[k] + a_msg[i-1].nsrand[k] * (a_msg[i].read_id - a_msg[i-1].read_id) * 2 * SEED_LEN;	//XXX SEED_LEN/opt_l
    int act = a_msg[i].offset[j];
    //dis <= 3 || >= -3: MATCH
    //dis > 3 :DELETION
    //dis < -3:INSERT

    int dis = a_msg[i-1].nsrand[k] * (act-exp);
    
    if (dis > 3) 
        *flag = DELETION;
    else if (dis < -3) 
        *flag = INSERT;
    else
        *flag = MATCH;
    dis=(dis>0?dis:(0-dis)); //Absolute value
    return adjest(dis);
}

int path_dp(aln_msg *a_msg, int n_seed)
{
    int i,j,k;
    int tmp, min;
    int flag;
    int rev_flag = 0;   //reverse

    //Initilization of first cloumn
    for (j = 0; j < a_msg[0].n_aln; j++)
    {
        price[0][j] = 0;
    }

    for (i = 1; i < n_seed; i++)    //n_seed条seed，相邻两条连接一条路径
    {
        for (j = 0; j < a_msg[i].n_aln; j++)
        {
            min = price[i-1][0] + get_min_dis(a_msg, i, j, 0, &flag);
            path[i][j].from = 0;
            path[i][j].flag = flag;
            for (k = 1; k < a_msg[i-1].n_aln; k++)
            {
                if ((tmp = price[i-1][k] + get_min_dis(a_msg, i, j, k, &flag)) < min)
                {
                    min = tmp;
                    path[i][j].from = k;
                    path[i][j].flag = flag;
                }
            }
            price[i][j] = min;
        }
    }

    return 0;
}

int backtrack(aln_msg* a_msg, path_msg path[][PER_ALN_N], int n_seed)  //from end to start, find every fragment's postion
{
    if (n_seed == -1)
    {
        fprintf(stdout, "frag: 0\n");    
        return 0;
    }
    //确定回溯起点
    int i;
    int min_pos=0, min_score=price[n_seed][0];
    for (i = 1; i < a_msg[n_seed].n_aln; i++)
    {
        if (price[n_seed][i] < min_score)
        {
            min_pos = i;
            min_score = price[n_seed][i];
        }
    }
    //回溯，确定fragment
    int frag_num = 1;
    int pos = min_pos;
    int flag = 0;
    //first end
    fprintf(stdout, "%d\t%c\t%d\t%d\t", a_msg[n_seed].chr[pos], a_msg[n_seed].nsrand[pos], a_msg[n_seed].offset[pos], a_msg[n_seed].read_id);    
    for (i = n_seed; i >=0; i--)
    {
        if (path[i][pos].flag != MATCH)
        {
            //start
            fprintf(stdout, "%d\t%d\n", a_msg[i].offset[pos], a_msg[i].read_id);    
            flag = 1;
            frag_num++;
        }
        pos = path[i][pos].from;
        if (flag == 1 && i > 0)  //获取一个fragment
        {
            //上一个fragment的end
            fprintf(stdout, "%d\t%c\t%d\t%d\t", a_msg[i-1].chr[pos], a_msg[i-1].nsrand[pos], a_msg[i-1].offset[pos], a_msg[i-1].read_id);
            flag = 0;
        }
    } 
    //last start
    fprintf(stdout, "%d\t%d\n", a_msg[0].offset[pos], a_msg[0].read_id);
	return 0;
}
int frag_cluster(char *seed_result, seed_msg *s_msg, int seed_len)
{
	FILE *result_p;
	char line[1024];
	int n_read/*from 1*/, n_seed;
	int32_t read_id, chr, offset;
	int8_t srand, edit_dis;
	aln_msg *a_msg;

	a_msg = aln_init_msg(s_msg->seed_max);

	if ((result_p = fopen(seed_result, "r")) == NULL)
	{
		fprintf(stderr, "[lsat_aln] Can't open file %s.\n", seed_result);
		exit(-1);
	}

	n_read = 0;
	int multi_aln = 1, last_id = 0, REPEAT = 0, FLAG=0;

	while (fgets(line, 1024, result_p) != NULL)
	{
		sscanf(line, "%d %d %d %c %d", &read_id, &chr, &offset, &srand, &edit_dis);
		if (read_id == last_id) {		// seeds from same read
			if (++multi_aln > PER_ALN_N) {
				if (!REPEAT) {
					n_seed--;
					REPEAT = 1;
				}
				continue;
			} else setAmsg(a_msg, n_seed, multi_aln, read_id - s_msg->n_seed[n_read-1], chr, offset, srand, edit_dis);
		} else {		//get a new seed
			REPEAT = 0;
			if (read_id > s_msg->n_seed[n_read]) {	//new read
				if (last_id != 0) {
					fprintf(stdout, "read %d n_seed %d\n", n_read, n_seed);
					path_dp(a_msg, n_seed);
					backtrack(a_msg, path, n_seed-1);
				}
				n_seed = 0;
				while (s_msg->n_seed[n_read] < read_id) {
					if (FLAG == 0) FLAG = 1;
					else fprintf(stdout, "read %d n_seed 0\nfrag: 0\n\n", n_read);
					n_read++;
				}
				FLAG = 0;
			}
		}
		multi_aln = 1;
		last_id = read_id;
		if (n_seed >= s_msg->seed_max)	{
			fprintf(stderr, "bug: n_seed > seed_max\n");
			exit(-1);
		}
		n_seed++;
		setAmsg(a_msg, n_seed, multi_aln, read_id-s_msg->n_seed[n_read-1], chr, offset, srand, edit_dis);
	}
	fprintf(stdout, "n_read %d n_seed %d\n", n_read, n_seed);
	path_dp(a_msg, n_seed);
	backtrack(a_msg, path, n_seed-1);

	fclose(result_p);
	aln_free_msg(a_msg, s_msg->seed_max);
	return 0;
}

int lsat_aln_core(char *ref_prefix, char *read_prefix, int no_soap2_dp, char *seed_result, char *opt_m, int opt_l)
{
	seed_msg *s_msg;
	char soap2_dp_cmd[1024];
	char path[1024];

	s_msg = seed_init_msg();
	split_seed(read_prefix, s_msg, opt_l);

	if (!strcmp(seed_result, ""))
	{
		strcpy(seed_result, read_prefix);
		strcat(seed_result, ".seed.out.0");
	}

	if (!no_soap2_dp)	//excute soap2-dp program
	{
		if (getcwd(path, sizeof(path)) == NULL)
		{
			perror("getcwd error");
			exit(-1);
		}
		sprintf(soap2_dp_cmd, "%s/soap-dp single %s.index %s.seed %s -h 2 ", path, ref_prefix, read_prefix, opt_m);
		printf("%s\n",soap2_dp_cmd);
		if (system(soap2_dp_cmd) != 0)
			exit(-1);
	}
	
	frag_cluster(seed_result, s_msg, opt_l);

	seed_free_msg(s_msg);
	return 0;
}

int lsat_aln(int argc, char *argv[])
{
	int c;
	int no_soap2_dp=0, opt_l=0;
	char result_f[1024], opt_m[100];
	char *ref, *read;
	
	opt_l = SEED_LEN;
	strcpy(opt_m, "-m 3e ");

	while ((c =getopt(argc, argv, "na:m:l:")) >= 0)
	{
		switch (c)
		{
			case 'n':
				no_soap2_dp = 1;
				break;
			case 'a':
				strcpy(result_f, optarg);	//soap2-dp alignment result
				break;
			case 'm':
				sprintf(opt_m, "-m %s ", optarg);
				break;
			case 'l':
				opt_l = atoi(optarg);
				break;
			default:
				return usage();
		}
	}
    if (argc - optind != 2)
		return usage();

	ref = strdup(argv[optind]);
	read =strdup(argv[optind+1]);

	lsat_aln_core(ref, read, no_soap2_dp, result_f, opt_m, opt_l);
	
	return 0;
}