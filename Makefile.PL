use strict;

require 5.6.0;

use ExtUtils::MakeMaker;
use Config;

if ((not $ARGV[0]) or (not $ARGV[1]))
{
	print "Usage: perl Makefile.PL <path-to-sendmail-source> <path-to-sendmail-obj.dir>\n";
	print "(e.g. 'perl Makefile.PL ../sendmail ../sendmail/obj.FreeBSD.4.0-RELEASE.i386')\n";
	print "\n";
	exit;
}

if (not $Config{usethreads})
{
	print "To use this module, your perl interpreter must have been compiled with\n";
	print "\t-Dusethreads.\n";
	print "\n";
	exit;
}


my $SENDMAIL_PATH = MM->canonpath($ARGV[0]);
my $SENDMAIL_OBJ_PATH = MM->canonpath($ARGV[1]);

my $MILTER_LIB = MM->catdir($SENDMAIL_OBJ_PATH, "libmilter");
my $MILTER_INCLUDE = MM->catdir($SENDMAIL_PATH, "include");
my $SENDMAIL_INCLUDE = MM->catdir($SENDMAIL_PATH, "sendmail");

WriteMakefile(
    'NAME'		=> 'Sendmail::Milter',
    'VERSION_FROM'	=> 'Milter.pm',
    'CCFLAGS'		=> '-pthread',
    'LIBS'		=> ["-L$MILTER_LIB -lmilter"],
    'OBJECT'		=> '$(BASEEXT)$(OBJ_EXT) intpools$(OBJ_EXT) callbacks$(OBJ_EXT)',
    'DEFINE'		=> '',
    'INC'		=> "-I$SENDMAIL_INCLUDE -I$MILTER_INCLUDE",
);

