/**

\file   replgetopt.h
\brief  Data structures for parsing command line options

AUTHOR: Gregory Pietsch
CREATED Thu Jan 09 22:37:00 1997

DESCRIPTION:

The getopt() function parses the command line arguments.  Its arguments argc
and argv are the argument count and array as passed to the main() function
on program invocation.  The argument optstring is a list of available option
characters.  If such a character is followed by a colon (`:'), the option
takes an argument, which is placed in optarg.  If such a character is
followed by two colons, the option takes an optional argument, which is
placed in optarg.  If the option does not take an argument, optarg is NULL.

The external variable optind is the index of the next array element of argv
to be processed; it communicates from one call to the next which element to
process.

The getopt_long() function works like getopt() except that it also accepts
long options started by two dashes `--'.  If these take values, it is either
in the form

--arg=value

 or

--arg value

It takes the additional arguments longopts which is a pointer to the first
element of an array of type option, defined below.  The last
element of the array has to be filled with NULL for the name field.

The longind pointer points to the index of the current long option relative
to longopts if it is non-NULL.

The getopt() function returns the option character if the option was found
successfully, `:' if there was a missing parameter for one of the options,
`?' for an unknown option character, and EOF for the end of the option list.

The getopt_long() function's return value is described below.

The function getopt_long_only() is identical to getopt_long(), except that a
plus sign `+' can introduce long options as well as `--'.

Describe how to deal with options that follow non-option ARGV-elements.

If the caller did not specify anything, the default is REQUIRE_ORDER if the
environment variable POSIXLY_CORRECT is defined, PERMUTE otherwise.

REQUIRE_ORDER means don't recognize them as options; stop option processing
when the first non-option is seen.  This is what Unix does.  This mode of
operation is selected by either setting the environment variable
POSIXLY_CORRECT, or using `+' as the first character of the optstring
parameter.

PERMUTE is the default.  We permute the contents of ARGV as we scan, so that
eventually all the non-options are at the end.  This allows options to be
given in any order, even with programs that were not written to expect this.

RETURN_IN_ORDER is an option available to programs that were written to
expect options and other ARGV-elements in any order and that care about the
ordering of the two.  We describe each non-option ARGV-element as if it were
the argument of an option with character code 1.  Using `-' as the first
character of the optstring parameter selects this mode of operation.

The special argument `--' forces an end of option-scanning regardless of the
value of `ordering'.  In the case of RETURN_IN_ORDER, only `--' can cause
getopt() and friends to return EOF with optind != argc.

COPYRIGHT NOTICE AND DISCLAIMER:

Copyright (C) 1997 Gregory Pietsch

This file and the accompanying getopt.c implementation file are hereby 
placed in the public domain without restrictions.  Just give the author 
credit, don't claim you wrote it or prevent anyone else from using it.

Gregory Pietsch's current e-mail address:
gpietsch@comcast.net
****************************************************************************/

#ifndef REPLGETOPT_H
#define REPLGETOPT_H

#define NEED_GETOPT 0
#define NEED_GETOPT_LONG 0
#ifdef _WIN32
	#undef NEED_GETOPT
	#define NEED_GETOPT 1
	#undef NEED_GETOPT_LONG
	#define NEED_GETOPT_LONG 1
#endif
/* A kludgy check to see if one of the getopt_long() symbols is defined. */
#ifndef no_argument
	#undef NEED_GETOPT_LONG
	#define NEED_GETOPT_LONG 1
#endif

/* include files needed by this include file */

#ifdef NEED_GETOPT_LONG
/* macros defined by this include file */
#define no_argument          0
#define required_argument    1
#define optional_argument    2

/* types defined by this include file */

/* option: The type of long option */
struct option
{
  char *name;                   /* the name of the long option */
  int has_arg;                  /* one of the above macros */
  int *flag;                    /* determines if getopt_long() returns a
                                 * value for a long option; if it is
                                 * non-NULL, 0 is returned as a function
                                 * value and the value of val is stored in
                                 * the area pointed to by flag.  Otherwise,
                                 * val is returned. */
  int val;                      /* determines the value to return if flag is
                                 * NULL. */
};
#endif

#ifdef __cplusplus
extern "C"
{
#endif

  /* externally-defined variables */
#if (NEED_GETOPT | NEED_GETOPT_LONG)
  extern char *optarg;
  extern int optind;
  extern int opterr;
  extern int optopt;
#endif

  /* function prototypes */
#if NEED_GETOPT
  int getopt (int argc, char **argv, const char * optstring);
#endif
#if NEED_GETOPT_LONG
  int getopt_long (int argc, char **argv, const char * shortopts,
                   const struct option * longopts, int * longind);
  int getopt_long_only (int argc, char * * argv, const char * shortopts,
                        const struct option * longopts, int *longind);
#endif

#ifdef __cplusplus
}

#endif

#endif /* GETOPT_H */

/* END OF FILE getopt.h */

