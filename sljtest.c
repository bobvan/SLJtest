/**
 * \file   sljtest.c
 * \brief  System Latency Jitter Test
 * \author Robert A. Van Valzah - Informatica Corporation
 *
 * System Latency Jitter Test - Measure and visualize system latency jitter
 *
 * See accompanying PDF or HTML renderings of doc, or see doc input at end of this file
 *  
 * Termnology used in the source:
 *  Ticks:
 *	One tick is the time taken for the Time Stamp Counter (TSC) on
 *	x86 processors to advance one count.
 *	The inverse of the CPU clock frequency is the time taken for
 *	one TSC tick in seconds.
 *
 *  Timestamp:
 *	One reading of TSC.
 *
 *  Block of timestamps:
 *	Timestamps taken in sequence to keep control flow uninterupted by branches.
 *	Statistics and logging are done after the block.
 *	This maximizes cache hit rates and keeps the CPU executing
 *	at full speed.
 *
 *  Delta:
 *	The difference between the TSC on two sequential readings.
 *
 *  Bin:
 *	One entry in the histogram table
 *
 *  Knee:
 *	The midpoint in the number of histogram bins: 1/2 the bins below
 *	the knee, 1/2 the bins above the knee.
 * 	Bins step linearaly below the knee and exponentially above it.
 *
 * ToDo
 *  Taskset
 *  CPU affiinity
 *  Maybe option to run many threads at once?
 *  Windows port
 *  Better comments on data structures and algoriths
 *  Break up main() so it's not so long
 *
 *
 * (c) Copyright 2011, 2012 Informatica Corp.
 * Robert A. Van Valzah, October 2011, February 2012, October 2012
 *
 * Acknowledgement
 *  Inspired by David Riddoch of Solarflare
 *
 * License
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted without restriction.  The author will
 * be disappointed if you use this software for making war, mulching
 * babies, or improving systems running competing messaging software.
 *
 * Disclaimers
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL
 *  WARRANTIES EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY
 *  IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS
 *  FOR A PARTICULAR PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE
 *  OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE.  INFORMATICA
 *  SHALL NOT, UNDER ANY CIRCUMSTANCES, BE LIABLE TO LICENSEE FOR LOST
 *  PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR INDIRECT DAMAGES
 *  ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE TRANSACTIONS
 *  CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
 *  THE LIKELIHOOD OF SUCH DAMAGES.
 *
 */

/*! Has side effect of declaring asprintf() */
#define	_GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#ifdef _WIN32
#include <windows.h>
#include "getopt.h"
#else /* _WIN32 */
#include "replgetopt.h"
#endif /* _WIN32 */

#ifdef	CPU_AFFINITY
#define	__USE_GNU
#include <sched.h>
#include "bitmask.h"
#endif	/* CPU_AFFINITY */

#if defined(_WIN32)
#include <sys/timeb.h>
#define WIN32_HIGHRES_TIME
extern int optind;
extern char *optarg;
int getopt(int, char *const *, const char *);
/*! Waste time for x      seconds */
#   define SLEEP_SEC(x) Sleep((x)*1000)
/*! Waste time for x microseconds */
#   define SLEEP_MSEC(x) Sleep(x)

#else
/*! Waste time for x      seconds */
#   define SLEEP_SEC(x) sleep(x)
/*! Waste time for x microseconds */
#   define SLEEP_MSEC(x) \
		do{ \
			if ((x) >= 1000){ \
				sleep((x) / 1000); \
				usleep((x) % 1000 * 1000); \
			} \
			else{ \
				usleep((x)*1000); \
			} \
		}while (0)
#endif /* _WIN32 */


/*! DEFault number of histogram BINS */
#define	DEF_BINS		20
/*! DEFault CPU affinity */
#define	DEF_CPU			NULL
/*! DEFault OUTput FILEname */
#define	DEF_OUTFILE		NULL
/*! DEFault KNEE value (ticks) */
#define	DEF_KNEE		50
/*! DEFault MINimum delta (ticks) */
#define	DEF_MIN			10
/*! DEFault OUTlier BUFfer */
#define	DEF_OUTBUF		10000
/*! DEFault PAUSE time before each block in microseconds */
#define	DEF_PAUSE		0
/*! DEFault RUN TIME in seconds */
#define	DEF_RUNTIME		1
/*! DEFault maximum output LINE WIDth */
#define	DEF_LINEWID		79

/*! \brief Read value of TSC into a uint64_t
 *  \param x A uint64_t to receive the TSC value
 */
#define rdtsc(x) \
	do { \
		uint32_t hi, lo; \
		asm volatile ("rdtsc" : "=a" (lo), "=d" (hi)); \
		x = (uint64_t)hi << 32 | lo; \
	} while (0)

/*! Size of array a in elements */
#define	ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

/*! Command line argument values */
typedef struct args_stct {
/*! Number of bins in histogram */
	int bins;
#ifdef	CPU_AFFINITY
/*! CPU affinity */
	char *cpu;
#endif	/* CPU_AFFINITY */
/*! OUTput FILEname */
	char *outfile;
/*! Knee of histogram curve (ticks) */
	uint64_t knee;
/*! Minimum expected value (ticks) */
	uint64_t min;
/*! OUTlier BUFfer size (outliers) */
	int outbuf;
/*! Pause before each block (milliseconds) */
	int pause;
/*! Runtime (seconds) */
	int runtime;
/*! Output line width (characters) */
	int linewid;
} args_t;

/*! Type for histogram table */
typedef struct bin_stct {
/*! Upper Bound (inclusive) on histogram bin (ticks) */
	uint64_t ub;
/*! Number of samples <= ub */
	int count;
} bin_t;

/*! Type for outlier buffer entry */
typedef struct outlier_stct {
/*! TSC when the outlier happended */
	uint64_t when;
/*! Size of outlier (ticks) */
	uint64_t delta;
} outlier_t;

/*! Command line argument values */
args_t args = {
	DEF_BINS,
#ifdef	CPU_AFFINITY
	DEF_CPU,
#endif	/* CPU_AFFINITY */
	DEF_OUTFILE,
	DEF_KNEE,
	DEF_MIN,
	DEF_OUTBUF,
	DEF_PAUSE,
	DEF_RUNTIME,
	DEF_LINEWID,
};

/*! Command line options for getopt() */
const struct option OptTable[] = {
	{"bins",    required_argument, NULL, 'b'},
#ifdef	CPU_AFFINITY
	{"cpu",     required_argument, NULL, 'c'},
#endif	/* CPU_AFFINITY */
	{"outfile", required_argument, NULL, 'f'},
	{"help",          no_argument, NULL, 'h'},
	{"knee",    required_argument, NULL, 'k'},
	{"min",     required_argument, NULL, 'm'},
	{"outbuf",  required_argument, NULL, 'o'},
	{"pause",   required_argument, NULL, 'p'},
	{"runtime", required_argument, NULL, 'r'},
	{"width",   required_argument, NULL, 'w'},
};

#ifdef	CPU_AFFINITY
const char *OptString = "b:c:f:hk:m:o:p:r:w:";
const char *usage = "[-b bins] [-c cpu] [-f file] [-h] [-k knee] [-m min] [-o outbuf] [-p pause] [-r runtime] [-w width]";

#else	/* CPU_AFFINITY */
const char *OptString = "b:f:hk:m:o:p:r:w:";
const char *usage = "[-b bins] [-f file] [-h] [-k knee] [-m min] [-o outbuf] [-p pause] [-r runtime] [-w width]";
#endif	/* CPU_AFFINITY */

/*! Note that Makefile parses the following line to extract version number */
const char *version = "SLJ Test 0.8c";

/*! Histogram table of timestamp deltas */
bin_t *histo;

/*! Ring BUFfer of recent OUTliers */
outlier_t *outbuf = NULL;
/*! FILE where we write OUTliers */
FILE *outfile = NULL;

#ifdef	CPU_AFFINITY
/*!
 * \brief Set CPU affinity
 * \param affinity CPUs for affinity expressed as a character string
 */
static void
set_affinity(const char* affinity) {
        struct bitmask *bm;

        fprintf(stderr, "Requesting CPU affinity on core %s\n", affinity);
	if ((bm=bitmask_alloc(__CPU_SETSIZE)) == NULL) {
		fprintf(stderr, "bitmask_alloc failed\n");
		exit(1);
	}
	if (bitmask_parselist(affinity, bm) != 0) {
		fprintf(stderr, "bitmask_parselist failed\n");
		exit(1);
	}
        if (sched_setaffinity(0, bitmask_nbytes(bm),
	    (cpu_set_t *)bitmask_mask(bm)) < 0) {
                fprintf(stderr, "%s: failed to set affinity to %s\n", affinity);
	}
}
#endif	/* CPU_AFFINITY */

#ifndef HAVE_ASPRINTF
/*! \brief sprintf() to a malloc()'d string
 *  \param ret Pointer to char * where output string will be returned
 *  \param format printf()-style format string
 *  \param ... Optional arguments a la printf()
 * \return 0
 */
/*! Our own hack version of asprintf() for platforms that don't have it.
 * I'm looking at you Solaris.
 */
int
#define	RET_MAX_LEN 10	/* Maximum length of return string */
asprintf(char **ret, const char *format, ...) {
	va_list ap;

	va_start(ap, format);
	if ((*ret=malloc(RET_MAX_LEN)) == NULL) {
		fprintf(stderr, "asprintf: malloc() failed\n");
		return(-1);
	}
	vsnprintf(*ret, RET_MAX_LEN, format, ap);
	va_end(ap);
	return(0);
}
#endif	/* HAVE_ASPRINTF */

/*!
 * \brief Convert a number of TSC ticks to a time string easily human readable.
 * \param ticks Ticks to be converted.
 * \param tpns  Ticks per nanosecond as measured for this system.
 *
 * Value is scaled to nanoseconds, microseconds, milliseconds, or seconds.
 * Returns a malloc()'d string, so caller must free() or leak.
 */
char *
t2ts(uint64_t ticks, double tpns) {
	double ns = ticks/tpns;
	char *s;

	     if (ns < 1E3 ) asprintf(&s, "%4.3gns", ns);
	else if (ns < 1E6 ) asprintf(&s, "%4.3gus", ns/1E3);
	else if (ns < 1E9 ) asprintf(&s, "%4.3gms", ns/1E6);
	else if (ns < 1E12) asprintf(&s, "%4.3gs",  ns/1E9);
	else                asprintf(&s, "Infini");

	return (s);
}

/*
 * \brief  Parse command line arguments.
 * \param  argc Count of arguments as passed to main().
 * \param  argv Argument vector as passed to main().
 * \return 0 if no parse errors, 1 otherwise.
 */
int
args_parse(int argc, char *argv[]) {
	int c;

	while ((c=getopt_long(argc, argv, OptString, OptTable, NULL)) != EOF) {
        	switch (c) {

		case 'b':
			args.bins    = atoi(optarg);
			break;

#ifdef	CPU_AFFINITY
		case 'c':
			args.cpu     = strdup(optarg);
			break;
#endif	/* CPU_AFFINITY */

		case 'f':
			args.outfile = strdup(optarg);
			break;

		case 'k':
			args.knee    = atoi(optarg);
			break;

		case 'm':
			args.min     = atoi(optarg);
			break;

		case 'p':
			args.pause   = atoi(optarg);
			break;

		case 'r':
			args.runtime = atoi(optarg);
			break;

		case 'w':
			args.linewid = atoi(optarg);
			break;

		case 'h':
		default:
			return (1);
		}
	}
	return (0);
}

/*! \brief Allocate memory and open file for outliers logging
 */
void
outliers_setup() {
	if ((outbuf=(outlier_t *)calloc(args.outbuf, sizeof(outlier_t))) == NULL) {
		fprintf(stderr, "Couldn't allocate memory for outlier buffer\n");
		exit(1);
	}
	if ((outfile=fopen(args.outfile, "w")) == NULL) {
		fprintf(stderr, "Unable to create outliers file %s\n",
		    args.outfile);
		perror(args.outfile);
		exit(1);
	}
}

void
histo_setup() {
	bin_t *bp;

	/* Allocate memory for histogram */
	if ((histo=(bin_t *)malloc(sizeof(bin_t)*args.bins)) == NULL) {
		fprintf(stderr, "Couldn't allocate memory for histogram bins\n");
		exit(1);
	}

	/*
	 * Fill first half of histogram table with values up to knee.
	 * Evenly divide upper bound values through knee among bins.
	 */
	for (bp=histo; bp<histo+(args.bins/2); bp++) {
		bp->ub = args.min+(args.knee-args.min)*(bp-histo+1)/(args.bins/2);
		bp->count = 0;
	}
	/* Fill second half of histogram table with values above knee */
	/* Advance each bin upper bound by 1/2 an order of magnitude */
	uint64_t mult = args.knee;
	for (        ; bp<histo+args.bins; bp++) {
		bp->ub = mult*2;
		bp->count = 0;
		bp++;

		mult *= 10;
		bp->ub = mult;
		bp->count = 0;
	}
	histo[args.bins-1].ub = UINT64_MAX;	/* Sentinel */
}

/*!
 * \brief Measure and visualize system latency jitter.
 * \param argc Count of arguments
 * \param argv Argument vector
 */
int
main(int argc, char *argv[]) {
	int errflag;
	uint64_t start_us, stop_us, now_us; /* Start, stop, and time now in microseconds */
	uint64_t start_tsc, stop_tsc;	/* Start and stop in TSC ticks */
	bin_t *bp;
	uint64_t deltas[10], *dp;
	uint64_t min, max;		/* Min and max deltas (ticks) */
	struct timeval now_gtod;	/* Time now as timeval */
	const char *pre_graph_hdr = "Time    Ticks    Count        Percent    Cumulative  ";
	const char *graph_str = "*******************************************************************";
	outlier_t *obp;		/* Pointer to next open entry in outlier buffer */
	int didwrap;		/* True when outlier buffer wrapped around */

	errflag = args_parse(argc, argv);

	/* Argument validity checks */
	if (args.knee <= args.min) {
		fprintf(stderr, "Min (%" PRIu64
		    ") must be < knee (%" PRIu64 ")\n",
		    args.min, args.knee);
		errflag++;
	}
	if (args.knee-args.min < args.bins/2) {
		fprintf(stderr, "Too few (%" PRIu64
		    ") discrete values between min (%" PRIu64
		    ") and knee (%" PRIu64 ") for linear histogram bins (%d)\n",
		    args.knee-args.min, args.min, args.knee, args.bins/2);
		errflag++;
	}
	if (args.linewid < strlen(pre_graph_hdr)+1) {
		fprintf(stderr, "Minimum line width is %zd\n", strlen(pre_graph_hdr)+1);
		errflag++;
	}
	if (args.linewid > strlen(pre_graph_hdr)+strlen(graph_str)) {
		fprintf(stderr, "Maximum line width is %zd\n",
		strlen(pre_graph_hdr)+strlen(graph_str));
		errflag++;
	}

	if (errflag) {
		fprintf(stderr, "%s\n%s %s\n", version, argv[0], usage);
		exit(1);
	}

#ifdef	CPU_AFFINITY
	if (args.cpu != NULL)
		set_affinity(args.cpu);
#endif	/* CPU_AFFINITY */

	if (args.outfile!=NULL && args.outbuf!=0) {
		outliers_setup();	/* Set up memory and output file for outliers */
	}

	histo_setup();		/* Set up histogram memory and data structures */

	/*
	 * XXX Note that it might get much eaiser to modularize this if in the
	 * future if we set up for multiple threads with a per-thread struct here.
	 */

	uint64_t timing_ticks = 0;	/* TSC ticks actually spent in timing measurements */
	uint64_t delta_count = 0;	/* Count of deltas taken */
	uint64_t delta_sum = 0;		/* Sum of TSC deltas measured */
	min   = UINT64_MAX;
	max   = 0;

	obp = outbuf;
	didwrap = 0;

	/*
	 * Variables for computing average and standard deviation.
	 * See http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#On-line_algorithm
	 */
	double avg = 0.0;	/* Average delta (ticks) */
	double last_avg;	/* Last average (needed for deviation */
	double svn = 0.0;	/* Standard Variance Numerator */

	rdtsc(start_tsc);
	gettimeofday(&now_gtod, NULL);
	start_us = now_gtod.tv_sec * 1000000 + now_gtod.tv_usec;
	stop_us = start_us + 1000000*args.runtime;

	do {
		dp = deltas;

		if (args.pause)
			SLEEP_MSEC(args.pause);

		register uint64_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10;

		/*
		 * Take a block of 11 timestamps and compute their differences into
		 * a 10-element array.
		 */
		/* Do an "unrolled loop" so there's no branching or other work */
		rdtsc(t0);
		rdtsc(t1);
		rdtsc(t2);
		rdtsc(t3);
		rdtsc(t4);
		rdtsc(t5);
		rdtsc(t6);
		rdtsc(t7);
		rdtsc(t8);
		rdtsc(t9);
		rdtsc(t10);
		*dp++ = t1 -t0;
		*dp++ = t2 -t1;
		*dp++ = t3 -t2;
		*dp++ = t4 -t3;
		*dp++ = t5 -t4;
		*dp++ = t6 -t5;
		*dp++ = t7 -t6;
		*dp++ = t8 -t7;
		*dp++ = t9 -t8;
		*dp++ = t10-t9;

		timing_ticks += t10-t0;

		/*
		 * Now that we're out of the timing loop, we can take all the
		 * CPU we need for analysis.
		 */
		for (dp=deltas; dp<deltas+ARRAY_SIZE(deltas); dp++) {
			if (*dp < min)
				min   = *dp;
			if (*dp > max)
				max   = *dp;

			/* Find bin to count this delta */
			/* Note: no end test is needed because of infinite sentinel */
			for (bp=histo; *dp>bp->ub; bp++) {}
			bp->count++;

			delta_count++;
			delta_sum += *dp;

			last_avg = avg;
			avg += ((double)*dp-avg)/delta_count;
			svn += ((double)*dp-avg)*((double)*dp-last_avg);

			/* If an outlier should be recorded */
			if (outbuf!=NULL && *dp>args.knee) {
				/* Assume it happened in middle of block */
				obp->when = t5;
				obp->delta = *dp;
				obp++;
				/* Wrap around if needed */
				if (obp-outbuf >= args.outbuf) {
					obp = outbuf;
					didwrap = 1;
				}
			}
		}
		rdtsc(stop_tsc);
		gettimeofday(&now_gtod, NULL);
		now_us = now_gtod.tv_sec * 1000000 + now_gtod.tv_usec;

	} while (now_us < stop_us);

	/* Compute Ticks Per NanoSecond for duration of test */
	double tpns = (stop_tsc-start_tsc)/1000.0/(stop_us-start_us);

	/* Print histogram headers */
	printf("%sGraph ln(Count-e)\n", pre_graph_hdr);

	/* Find maximum count in any histogram bin for scaling graph */
	uint64_t max_count = 0;
	for (bp=histo; bp<histo+args.bins; bp++) {
		if (bp->count > max_count)
			max_count = bp->count;
	}
	double graph_scale = (double)(args.linewid-strlen(pre_graph_hdr))/log(((double)max_count)-M_E);

	/* Print histogram */
	uint64_t c_count = 0;	/* Cumulative count as we step through bins */
	uint64_t mid_count = 0;	/* Cumulative count at histogram midpoint */
	for (bp=histo; bp<histo+args.bins; bp++) {
		/*
		 * If at midpoint in histogram, record c_count for
		 * knee tuning advice later.
		 */
		if (bp == histo+(args.bins/2))
			mid_count = c_count;
		c_count += bp->count;

		/* Format bin upper bound nicely into ub_str */
		char *ub_str, ubbuf[99];
		if (bp->ub == UINT64_MAX)
			ub_str = "Infinite";
		else {
			sprintf(ubbuf, "%-8" PRIu64, bp->ub);
			ub_str = ubbuf;
		}

		/* Compute a logrithmic function on bin count that looks good */
		int countlog = graph_scale*log(((double)bp->count)-M_E);
		if (countlog < 0)
			countlog = 0;
		/*
		 * Any nonzero count deserves a star, even though floating
		 * point sometimes rounds down to zero stars.
		 */
		if (countlog==0 && bp->count!=0)
			countlog = 1;

		/* Print a row for each bin */
		printf("%s  %s %-12d %7.4f%%  %8.4f%%    %*.*s\n",
		    t2ts(bp->ub, tpns), ub_str, bp->count,
		    100.0*bp->count/delta_count, 100.0*c_count/delta_count,
		    countlog, countlog, graph_str);

		/* Separate lower 1/2 of histogram from upper 1/2 */
		if (bp-histo+1 == args.bins/2)
			printf("\n");
	}

	/*
	 * Population variance is svn sum computed above over size of population.
	 * Population standard deviation is square root of population variance.
	 */
	double std_dev = sqrt(svn/delta_count);

	/* Print some useful statistics */
	printf("\nTiming was measured for %s, %5.2f%% of runtime\n",
	    t2ts(timing_ticks, tpns), 100.0*timing_ticks/(stop_tsc-start_tsc));
	printf("CPU speed measured  : %7.2f MHz over %" PRIu64 " iterations\n",
	    (double)(stop_tsc-start_tsc)/(stop_us-start_us), delta_count);
	printf("Min / Average / Std Dev / Max :   %" PRIu64 "   /   %" PRIu64 "   /  %3.0f   / %" PRIu64 " ticks\n",
	    min, delta_sum/delta_count, std_dev, max);
	printf("Min / Average / Std Dev / Max : %s / %s / %s / %s\n",
	    t2ts(min, tpns), t2ts(delta_sum/delta_count, tpns),
	    t2ts((uint64_t)std_dev, tpns), t2ts(max, tpns));

	/* Analyze histogram to give advice on setting better min and knee */
	if (min<args.min || args.min<0.80*min) {
		printf("Recommend min setting of %3.0f ticks\n", 0.80*min);
	}
	if (outbuf==NULL && 100.0*mid_count/delta_count<90.0) {
		printf("Recommend increasing knee setting from %" PRIu64 " ticks\n",
		    args.knee);
	}
	if (outbuf==NULL && 100.0*mid_count/delta_count>99.0) {
		printf("Recommend decreasing knee setting from %" PRIu64 " ticks\n",
		    args.knee);
	}

	/* Dump log of outliers to outfile */
	/* XXX Why isn't this a compound && test like the above call to outliers_setup()? */
	if (outbuf != NULL) {
		if (didwrap) {
			printf("Recommend increasing knee setting from %" PRIu64 " ticks\n",
			    args.knee);
		} else if (obp-outbuf < args.outbuf/4) {
			printf("Recommend decreasing knee setting from %" PRIu64 " ticks\n",
			    args.knee);
		}
		for (obp=outbuf; obp<outbuf+args.outbuf; obp++) {
			if (obp->when == 0)
				continue;
			fprintf(outfile, "%f, %f\n",
			    (obp->when-start_tsc)/tpns/1000000.0,
			    obp->delta/tpns/1000.0);
		}
		fclose(outfile);
	}
	return (0);
}
/**
\mainpage System Latency Jitter Test
\tableofcontents
 
\section impatient For the Impatient

\li Run a pre-compiled binary:<tt> bin/\<Platform\>/sljtest</tt>
\li On Windows, open a <tt>cmd</tt> window and run <tt>sljtext.exe</tt> in that.
\li Or build from source and run:<tt> cd src; make; ./sljtest</tt>
\li The output should be pretty self explanatory.
\li Put your right ear on your shoulder to see the histogram.
\li See \ref output "Output Explanation" below for details.
\li See \ref options "Command Line Options" below for fine tuning and features.
\li See \ref examples "Example Output" below for example output and analysis.
\li Browse code by <a href="globals.html"><b>functions, variables, defines, enums, and typedefs</b></a>.

\warning This is a beta release. The code and doc will improve with time,
but should be useful even now thanks to alpha testers.
Please send all comments to BVan@Informatica.Com.

\section introduction Introduction

Ultra Messaging customers use our software to build complicated
systems that often process millions of messages per second.  Common
goals are low latency and high throughput, but many customers also
value freedom from latency jitter.  A system with no latency jitter would
show exactly the same latency for every message passing through it.
Consistency is the hallmark of low-jitter systems.

It's hard to be consistent in doing repetitive work when you're being
interrupted all the time.  For humans, consistency in performing a
repeated task requires freedom from interruptions.

Roughly the same idea applies to computer systems.
SLJ Test measures the ability of a system to provide a CPU core that is
able to consistently execute a repetitive task.
The emphasis is <em>not</em> on the average time taken to do a task but
rather on the <em>variance</em> in time between repetitions of the task.

We will define system latency jitter more thoroughly
\ref sources_categorization "below",
but for now we can think of it as the jitter measured by an application
that introduces no jitter of its own.
System latency jitter will be present in any application run by the system.
It represents the lower bound for jitter that might be expected of
any application running on the system.

SLJ Test measures system latency jitter and provides a simple but effective
visualization of it for analysis.

Running a system latency jitter benchmark under different conditions can provide
insight into the causes of system latency jitter.
Here are some test ideas:

\li Compare results with other CPU cores idle and with them working
\li Compare the same OS running on different server hardware
\li Compare different OSes running on the same server hardware
\li Compare an OS running native to an OS running on a Hypervisor
\li Compare BIOS settings for their jitter impact
\li Compare single-user mode to multi-user mode
\li Compare short test runs to long test runs
\li Compare a sleeping thread to a hot thread burning CPU time
\li Compare various CPU cores to each other
\li Compare hyperthreaded shared cores to unshared cores
\li Compare benefits of cset, taskset, etc.


\section design Design

The design of SLJ Test was motivated by goals, but bounded by constraints.

\subsection goals Design Goals

Two key design goals drove the development of SLJ Test:

\li Make measurements to quantify system latency jitter.
These measurements can be used as benchmarks
for performance comparisons between systems or to guide tuning efforts.
\li Provide a visualization of collected data that aids in analysis
and characterization of system latency jitter.
The visualization can help drive jitter tuning efforts.

\subsection constraints Design Constraints

The above goals had to be met within several design constraints.

\li <em>Test Speed</em>
Often in our experience, reducing jitter is a process of test, hunch,
tune, and retest.
It is sometimes possible to quickly find and remove a source of
jitter, but more often, repeated
tuning and testing are required.
Hence SLJ Test was designed
to produce quick results so that the effect of tuning changes could be
tested quickly.

\li <em>Simplicity</em>
Benchmarking and tuning opportunities are often fleeting, so it's important
to get accurate results and actionable information simply.
SLJ Test combines data collection, analysis, and visualization in a
single tool that is easy to operate.

\li <em>Portability</em>
It is useful to be able to compare results across a variety of operating systems.
SLJ Test is a small code base that uses few system libraries for good portability.
It requires an x86 processor for access to the RDTSC instruction.
It comes with pre-compiled binaries for Linux, Solaris, Mac OS X, and FreeBSD.

\li <em>Information Density</em>
Even a 1-second test run produces tens of millions of data points.
Visualization of these data points can be key to forming strategies for
jitter reduction.
The range of data values can easily span 6 orders of magnitude, so they
can be difficult to represent on even a high-resolution display.
The constraints of simplicity and portability are best met with a
character interface rather than a graphical one.
So SLJ Test aims to analyze and visualize all the data using easily
portable character graphics.


\section methodology Time Measurement Methodology

The CPU's
<a href="http://en.wikipedia.org/wiki/Time_Stamp_Counter">Time Stamp Counter</a>
(TSC) is useful for measuring how
long tasks take, especially when very short times are involved.
It's usually counting at a rate of 3 or 4 billion counts per second.
That gives it a resolution of 333 to 250 picoseconds.

Elapsed ticks can be measured by reading the TSC before and after
doing a task, then subtracting the timestamps.
This gives the number of ticks taken by the task, plus the time
taken to read the TSC once.

\code
	uint64_t start, stop, elapsed_ticks;

	rdtsc(start);
	// Do some task
	rdtsc(stop);
	elapsed_ticks = stop - start;
\endcode

When elapsed ticks for an interval are known,
the elapsed time in seconds can be computed by
dividing elapsed ticks by the CPU frequency in Hertz.

When elapsed time for an interval is known,
CPU frequency in Hertz can be computed by
dividing the elapsed ticks by the elapsed time.

If a task is repeated many times while measuring elapsed ticks,
there will undoubtedly be
some variation (jitter) in the number of ticks taken to do the task, even
though the work should be the same each time for tasks with no
conditional logic.

\section strategy Jitter Measurement Strategy

Jitter is most apparent when we measure the elapsed time taken to
do quick tasks.  The limiting case is to measure the time needed
to just read the TSC.  Measuring the delta between two adjacent
readings of the TSC does this.  It is equivalent to measuring
the elapsed time taken by a null task, plus the time to read the TSC.

We want to avoid conditional logic since it may cause jitter by varying
CPU cache hit rates.
We want to quickly take as many timestamp pairs as possible to maximize
jitter detection opportunities.
Both these goals are met by using manual
<a href="http://en.wikipedia.org/wiki/Loop_unwinding">loop unwinding</a>
to collect the timestamps.
Analysis work on the collected timestamps is never done while collecting,
but only after completion of the unwound loop.

\code
	uint64_t deltas[10], *dp;

	do {
		dp = deltas;

		register uint64_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10;

		// Take a block of 11 timestamps and compute their differences into
		// a 10-element array.
		//
		// Do an "unwound loop" so there's no branching or other work

		rdtsc(t0);
		rdtsc(t1);
		rdtsc(t2);
		. . .
		rdtsc(t9);
		rdtsc(t10);

		*dp++ = t1 - t0;
		*dp++ = t2 - t1;
		. . .
		*dp++ = t10 - t9;

		// Now that we're out of the timing loop, we can take all the
		// CPU time that we need for analysis.

		for (dp=deltas; dp<deltas+ARRAY_SIZE(deltas); dp++) {
			// Analysis as needed
		}
	} while (collecting);
\endcode

\section sources_categorization Jitter Sources and Categorization

It's true that each repeated test would run at the minimum elapsed time if
there were no jitter.
Any test repetition that takes longer than the minimum was delayed by waiting
for some system resource.
It had to wait because there was contention for a resource that was already
in use elsewhere and could not be shared.
Examples of shared resources might be data paths on the CPU chip, hyperthreaded
execution units, shared caches, memory busses, CPU cores, kernel data structures
and critical sections.
So it's correct but simplistic to say that all jitter comes from contention.

We can make better progress at reducing jitter if we identify discrete
sources of jitter and categorize them for discussion and measurement.

The CPU scheduler in the OS can be a source of jitter if a thread
is moved between CPU cores since cache misses in the new core will
initially slow execution compared to the old core.  Virtual machine
hypervisors may prevent a thread from holding the attention of a
CPU core indefinitely.  Even things at the CPU hardware level like
shared cache contention and hyperthreading can add jitter.

Jitter in elapsed time measurements for doing identical tasks is
also a measure of things distracting a CPU core from running a
thread.  Some runs of a task may take longer than others because
a CPU core is not able to finish the task without first doing
additional work.  Hardware interrupts are one possible source of
such additional work, but it's best to think of interruptions more
broadly.  Anything that keeps a CPU core from executing at full
speed is a distraction that hurts performance and adds jitter.

Since jitter sources exist at many layers of a system, we can envision
a stack of jitter sources by layer as shown below:

\image html jitter_layers.png "Figure 1: Jitter by layers and sources, showing sums."

Note that jitter measurements from lower layers propagate to higher layers.

\subsection system_jitter Defining System Latency Jitter

This code aims to measure system latency jitter
so that a system can be tuned to minimize it.
Reducing jitter in
elapsed time measurements made with the TSC minimizes jitter
throughout the system.  Reading the TSC value is a very low-level
operation.  It is independent of higher-level languages and libraries
like Ultra Messaging.

We define system latency jitter as the jitter
measured at the lowest-possible level at which messaging or
application software can be written.
Applications are the likely source of any jitter measured in
excess of the system latency jitter.

SLJ Test uses no network I/O, no messaging library, no
memory-managed language, or abstraction layers.  It's measuring
jitter as close to the machine hardware as we know how to get with
C and in-line assembler.  Said differently, jitter measured by SLJ Test
is coming from server hardware, BIOS, CPU, VM, and OS sources.


\subsection application_jitter Contrast with Application Jitter

Applications doing repetitive tasks may introduce their own jitter
by slightly varying the task.
Conditional execution in the form of branches and loops is an obvious
source of jitter because of cache misses and other effects.
Even when code paths appear to be free from branches and loops, jitter can be
caused by library calls like \c malloc() and \c free()
that necessarily contain their own conditional code paths.
We categorize all such jitter as application jitter.

Adding networking, messaging libraries, memory-managed language
wrappers, and/or abstraction layers will only magnify the effects
of system latency jitter.
One microsecond of jitter on each step of a 1000-step task can turn into
one millisecond of jitter for the whole task.

Note that we use the term "jitter" here in the general sense meaning
variation from an expected norm.  The system latency jitter discussed
here is just one component of the latency variation observed in
application-level tests like repeated message round-trip time tests.
The standard deviation of latency measured in message round-trip
times is often loosely called "jitter," but we are making a distinction
here between that and system latency jitter.
System latency jitter is that component that can never be removed from an application
jitter measurement because it is present in all applications running
on the system.

\section visualization Jitter Visualization

Even a short system latency jitter test produces many millions of data points, so
a concise visualization of the data is required to quickly interpret the results.
Specifically, a good visualization of the data would allow quick and meaningful
comparisons with other test runs.

A <a href="http://en.wikipedia.org/wiki/Histogram">histogram</a> is the natural
choice for visualizing system latency jitter test data.
Histograms are commonly used in applications like displaying the variance in
adult height among a population.
However, inherent differences in system latency jitter data and display constraints
suggest we make some changes from common histograms.
Specifically, we use non-uniform bin spacing, logarithmic value display, and
rotated axes.
Details are presented in following sections.

\subsection data_characteristics Jitter Data Characteristics

Although many natural processes
follow a normal distribution, the elapsed times measured in system latency jitter
tests are not expected to follow a normal distribution.
One reason is that there is a hard lower bound on the time required
to read the TSC so we shouldn't expect a symmetric or uniform
distribution around the mean.  Indeed in most cases, the mean and
at least one mode are
quite close to the lower bound with a very long tail of outliers.
Another reason is that the different
processes that introduce jitter often produce multimodal distributions.

We often see two or more closely-spaced modes near the lower bound
and then several more widely-spaced modes that are perhaps orders
of magnitude greater.
Conventional linear histogram bin spacing would not visualize this data
well.
Small numbers of bins would combine nearby modes that have distinct causes.
Large numbers of bins leave many bins empty and make it difficult to see
patterns.

A high-resolution display might be able to
distinctly show both closely- and widely-spaced modes with consistent
scaling, but it's hard to write portable code for
high-resolution displays.

\subsection knee Observing a Knee in the Data

A graph of the cumulative percentage of all samples always rises from 0 to 100% as
we move across the histogram bins.
With system latency jitter data, we often see the cumulative percentage rise quickly from
0 to >90% in just a few bins, but then rise much more slowly after that.
Thus a graph of the cumulative percentage often shows a <em>knee</em> range where
the growth per bin slows.

\subsection bin_spacing Histogram Bin Spacing

We make it
easier for modes to be seen by using two different bin spacings
in the same histogram.
The number of bins in the histogram is divided into two halves.
The lower half of the bins are lineraly
spaced while the upper half are exponentially spaced.  This provides
detail near the lower bound where modes are closely spaced, while
preserving the range needed to show modes that occur 5 or more
orders of magnitude above the mean.
For linguistic convenience,
the bins in the lower half of the histogram are said to be "below
the knee," while those in the upper half are above.

The hard lower bound mentioned above means that bins near 0 will likely
be empty.
We define a minimum expected value that improves the resolution below
the knee.
Any values below the expected minimum are accumulated in the first bin.

Command line options allow setting the number of bins, the knee
value, and a minimum value which is the lower bound of the first
bin. Here is an example for 20 histogram bins with a minimum value of
10% of the knee value:

\verbatim
 Bin		Range
 0		min +  0- 10% * (knee-min)
 1		min + 10- 20% * (knee-min)
 2		min + 20- 30% * (knee-min)
 3		min + 30- 40% * (knee-min)
 4		min + 40- 50% * (knee-min)
 5		min + 50- 60% * (knee-min)
 6		min + 60- 70% * (knee-min)
 7		min + 70- 80% * (knee-min)
 8		min + 80- 90% * (knee-min)
 9		min + 90-100% * (knee-min)
 -		--------------------------
 10		          1-2 * knee
 11		         2-10 * knee
 12		        10-20 * knee
 13		       20-100 * knee
 14		      100-200 * knee
 15		     200-1000 * knee
 16		    1000-2000 * knee
 17		   2000-10000 * knee
 18		  10000-20000 * knee
 19		 20000-100000 * knee
\endverbatim

\section output Output Explanation

Default values produce output that is 28 lines by no more than 80 characters.
This small space contains areas with many statistics and
a compact visualization of system latency jitter.

The following figure gives a quick overview of the 5 output areas while
following sections give detail on each.

\image html output_explanation.png "Figure 2: Output Explanation."

\subsection histogram_display Histogram Display

The histogram is displayed rotated 90 degrees clockwise from a
conventional display to keep the code portable across operating
systems and avoid the requirement for a high-resolution graphical
display.
The histogram bins are separated by elapsed time and are displayed vertically
one bin per line instead of the more conventional horizontal display.
The count of the
samples in each bin are displayed horizontally instead of the more
conventional vertical display.

Note that the number of counts
per bin is displayed on a logarithmic scale so that
it will fit on low-resolution screens.
The magnitude (width)
of all bins is scaled so that the bin with the largest count uses
the full line width (default 80).

The first half of the bins below the knee
setting are linearly spaced in the hope of catching the mode,
the average, and the vast majority of the samples.
The second half of the bins are spaced
roughly by half orders of magnitude so that every pair of bins
represents one order of magnitude.

So the top-half (left-half) of the histogram is a
<a href="http://en.wikipedia.org/wiki/Semi-log">semi-log plot</a>
while the
bottom-half (right-half) is a
<a href="http://en.wikipedia.org/wiki/Log-log_plot">log-log plot</a>.

\subsection statistics Statistics

Each line of output contains statistics for the associated histogram bin
and a visualization of the data.
Column values are given in the table below.

Column     | Contents
:--------- | :-------
Time       | The upper bound of this bin in terms of elapsed time between adjacent reads of the TSC.
Ticks      | The same as above, but in ticks of the TSC.
Count      | A count of the number of samples in this bin.
Percent    | The percentage of all samples in this bin.
Cumulative | The cumulative percentage of all samples in this bin and lower.
Graph      | A graph of the Count column.

If you want to see more detail in the histogram and have a window with
many rows, try increasing the number of bins.
Or try increasing the output line width if you have a window with more
than 80 columns.

After per-bin statistics and histogram lines, there is a section of statistics
for the overall test run.
While the histogram is useful for characterizing a system's latency jitter and tuning, the 
overall statistics may be more useful for making benchmark comparisons between
systems.
In particular, the maximum and standard deviation in units of time are probably
the two most important numbers for most Ultra Messaging customers.

\subsection recommendations Recommended Test Parameters

Design goals and constraints drove the decision to combine data collection and
visualization into a single step.
This creates a bit of a chicken-and-egg problem because 
minimum and knee values must be set before data collection yet the optimal values
cannot be known until after data collection and often visualization.
The default minimum and knee values may be good enough for a quick analysis, but
tuning these values away from the defaults often provides sharper insight.

There are heuristics built into SLJ Test that check the minimum and knee values
for reasonability after data collection.
You'll see this recommendation if
the minimum data sample is less that 80% of the configured expected minimum.

\verbatim
Recommend min setting of x ticks
\endverbatim

Where \a x is 80% of the minimum data sample seen.

Similarly you'll see this recommendation if the cumulative sample count at the
middle histogram bin is less than 90% the total samples.

\verbatim
Recommend increasing knee setting from x ticks
\endverbatim

Where \a x is the configured knee value.

Finally you'll see this recommendation if the cumulative sample count at the 
middle histogram bin is greater than 99% of the total samples.

\verbatim
Recommend decreasing knee setting from x ticks
\endverbatim

Where \a x is the configured knee value.

\section logging Logging and Plotting Outliers

An outlier is defined as any TSC delta greater than the knee.  The <tt>-f</tt> option
names a file where outliers will be written.  The format is \a x, \a y
where \a x is the time of the outlier in ms relative to the start of
the test and \a y is the size of the outlier in us.  Note the different
units between axes.

The expectation is that you'll give this data to the graphing
software of your choice and request an \a x \a y scatter plot.  Visual
analysis of the plot may help you spot any periodic patterns in
the jitter.  The period may give you clues to the source of the
jitter.

\image html NoTimeCorrelation.png "Figure 3: Outliers with no apparent time correlation."

Better yet, do an FFT on the data to move it from the time domain
to the frequency domain.

Note that \a x may not be near zero if the outlier buffer wraps
around.  If you're worried about the outlier buffer wrapping around,
my advice is to increase the knee to classify fewer deltas as
outliers rather than making the buffer bigger.  The default size
is probably big enough for you to spot any periodic patterns.


\section options Command Line Options

\verbatim
 -b bins	Set the number of Bins in the histogram (20)
 -f outfile	Name of file for outlier data to be written (no file written)
 -h		Print Help
 -k knee	Set the histogram Knee value in TSC ticks (50)
 -m min		Set the Minimum expected value in TSC ticks (10)
 -o outbuf	Size of outlier buffer in outliers (10000)
 -p pause	Pause msecs just before starting jitter test loop (0)
 -r runtime	Run jitter testing loops until seconds pass (1)
 -w width	Output line Width in characters (80)
\endverbatim

\section examples Example Output

Following sections show data collected on various systems under
various conditions with comments.

\subsection low_jitter Low-Jitter Server

This output came from a server in our lab that had been tuned for low jitter.

\verbatim
Time    Ticks    Count        Percent    Cumulative  Graph ln(Count-e)
10.7ns  32       0             0.0000%    0.0000%    
11.4ns  34       0             0.0000%    0.0000%    
  12ns  36       6997631      42.0733%   42.0733%    *************************
12.7ns  38       0             0.0000%   42.0733%    
13.4ns  40       0             0.0000%   42.0733%    
  14ns  42       0             0.0000%   42.0733%    
14.7ns  44       0             0.0000%   42.0733%    
15.4ns  46       9634329      57.9265%   99.9998%    **************************
  16ns  48       0             0.0000%   99.9998%    
16.7ns  50       0             0.0000%   99.9998%    

33.4ns  100      0             0.0000%   99.9998%    
 167ns  500      0             0.0000%   99.9998%    
 334ns  1000     2             0.0000%   99.9998%    *
1.67us  5000     36            0.0002%  100.0000%    *****
3.34us  10000    2             0.0000%  100.0000%    *
16.7us  50000    0             0.0000%  100.0000%    
33.4us  100000   0             0.0000%  100.0000%    
 167us  500000   0             0.0000%  100.0000%    
 334us  1000000  0             0.0000%  100.0000%    
Infini  Infinite 0             0.0000%  100.0000%    

Timing was measured for  229ms, 22.91% of runtime
CPU speed measured  : 2992.58 MHz over 16632000 iterations
Min / Average / Std Dev / Max :   36   /   41   /    6   / 6984 ticks
Min / Average / Std Dev / Max :   12ns / 13.7ns / 1.67ns / 2.33us
\endverbatim

42% of the samples were in the 12 ns bin and almost 58% were in the 15.4 ns bin.
Together, 99.9998% of all samples were in these two bins.
There were zero samples in the 4 bins between these two, producing a nice
bi-modal histogram.
The outlier samples were just 36 out of 16.6 million. 

\subsection busy_server Busy Server

This output came from a busy server in our lab.

\verbatim
Time    Ticks    Count        Percent    Cumulative  Graph ln(Count-e)
6.92ns  23       18332137     67.6263%   67.6263%    **************************
7.82ns  26       788340        2.9081%   70.5344%    *********************
8.72ns  29       2679472       9.8844%   80.4189%    ***********************
9.62ns  32       414           0.0015%   80.4204%    *********
10.5ns  35       63212         0.2332%   80.6536%    *****************
11.4ns  38       4972436      18.3431%   98.9966%    ***********************
12.3ns  41       90            0.0003%   98.9970%    ******
13.2ns  44       54281         0.2002%   99.1972%    ****************
14.1ns  47       216764        0.7996%   99.9968%    *******************
  15ns  50       306           0.0011%   99.9980%    ********

30.1ns  100      4             0.0000%   99.9980%    *
 150ns  500      3             0.0000%   99.9980%    *
 301ns  1000     0             0.0000%   99.9980%    
 1.5us  5000     173           0.0006%   99.9986%    *******
3.01us  10000    315           0.0012%   99.9998%    ********
  15us  50000    52            0.0002%  100.0000%    ******
30.1us  100000   1             0.0000%  100.0000%    *
 150us  500000   0             0.0000%  100.0000%    
 301us  1000000  0             0.0000%  100.0000%    
Infini  Infinite 0             0.0000%  100.0000%    

Timing was measured for  188ms, 18.85% of runtime
CPU speed measured  : 3324.96 MHz over 27108000 iterations
Min / Average / Std Dev / Max :   18   /   23   /   42   / 88982 ticks
Min / Average / Std Dev / Max : 5.41ns / 6.92ns / 12.6ns / 26.8us
\endverbatim

It shows a much faster average time than the \ref low_jitter "low-jitter server"
above, but the standard deviation is over 7 times larger while the maximum
is over 11 times larger.

\section availability Availability

Informatica makes the binary and source code for SLJ Test freely available so that our
customers can use it to work with their hardware and software
vendors to reduce jitter.  We pioneered this idea with our
<a href="https://community.informatica.com/solutions/informatica_mtools">mtools</a>
software for testing multicast UDP performance.  Many customers
have used mtools to work with their NIC, driver, OS, and server
vendors to improve UDP multicast performance.  We hope that this
code can be used between our customers and their other vendors to
reduce system latency jitter.


\copyright (c) Copyright 2011, 2012 Informatica Corp.

\author Robert A. Van Valzah


\section acknowledgement Acknowledgement

Inspired by David Riddoch of Solarflare


\section license License

Redistribution and use in source and binary forms, with or without
modification, are permitted without restriction.  The author will
be disappointed if you use this software for making war, mulching
babies, or improving systems running competing messaging software.


\section disclaimers Disclaimers

THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL
WARRANTIES EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY
IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS
FOR A PARTICULAR PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE
OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE.  INFORMATICA
SHALL NOT, UNDER ANY CIRCUMSTANCES, BE LIABLE TO LICENSEE FOR LOST
PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR INDIRECT DAMAGES
ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE TRANSACTIONS
CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
THE LIKELIHOOD OF SUCH DAMAGES.

*/
