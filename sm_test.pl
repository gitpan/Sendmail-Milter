use Cwd;

our $ConfigFilename = "test-milter.cf";
our $MilterName = "myfilter";


sub make_connection
{
	my $test_connection = "";
	my $user = getpwuid($<);

	$test_connection .= <<"EOF";
HELO localhost
MAIL From: <$user\@localhost>
RCPT To: <$user\@localhost>
DATA
From: $user\@localhost
To: $user\@localhost
Subject: testing sample filter

------------------------------------------------------------------------
Test Data
------------------------------------------------------------------------
.
QUIT
EOF

	return $test_connection;
}

sub run_sendmail
{
	my $cmdline = "sendmail";
	my $test_connection;

	$cmdline .= " -C$ConfigFilename";
	$cmdline .= " -O QueueDirectory=" . getcwd;
	$cmdline .= " -O InputMailFilters=$MilterName";

	$cmdline .= " -bs -v";

	$test_connection = make_connection();

	open (WRITESENDMAIL, "| $cmdline");

	print WRITESENDMAIL $test_connection;

	close (WRITESENDMAIL);
}

sub create_milter_cf
{
	open(SENDMAILCF, "/etc/mail/sendmail.cf");
	open(MILTERCF, ">$ConfigFilename");

	while(defined($_ = <SENDMAILCF>))
	{
		print MILTERCF;
	}

	print MILTERCF "X$MilterName, S=inet:3333\@localhost\n";

	close(MILTERCF);
	close(SENDMAILCF);
}

sub run_milter
{
	exec("$^X sample.pl $MilterName $ConfigFilename");
}

BEGIN:
{
	print "$^X\n";
	print "This program will send a test message to you through a mail filter.\n";

	create_milter_cf();

	if ($child_pid = fork)
	{
		# Parent
		print "Waiting for $MilterName to start...\n";
		sleep(1);

		print "Launching sendmail to send test message...\n";

		run_sendmail();

		# Sendmail's done, kill the milter.
		kill (1, $child_pid);

		print "Waiting for $MilterName to finish...\n";
		waitpid ($child_pid, 0);

		unlink($ConfigFilename);
	}
	else
	{
		# Child
		run_milter();
	}
}
