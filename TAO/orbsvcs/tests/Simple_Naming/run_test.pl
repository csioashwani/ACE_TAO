eval '(exit $?0)' && eval 'exec perl -S $0 ${1+"$@"}'
    & eval 'exec perl -S $0 $argv:q'
    if 0;

# $Id$
# -*- perl -*-

# This is a Perl script that runs the client and all the other servers that
# are needed

unshift @INC, '../../../../bin';
require Process;
require ACEutils;
require Uniqueid;

# amount of delay between running the servers

$sleeptime = 8;

# variables for parameters

$nsmport = 10000 + uniqueid ();
$iorfile = "ns.ior";

sub name_server
{
  my $args = "-ORBnameserviceport $nsmport -o $iorfile";
  my $prog = "..$DIR_SEPARATOR..$DIR_SEPARATOR".
    "Naming_Service".$DIR_SEPARATOR.
      "Naming_Service".$Process::EXE_EXT;

  unlink $iorfile;
  $NS = Process::Create ($prog, $args);

  if (ACE::waitforfile_timed ($iorfile, $sleeptime) == -1) {
    print STDERR "ERROR: cannot find IOR file <$iorfile>\n";
    $NS->Kill (); $NS->TimedWait (1);
    exit 1;
  }
}

sub client
{
  my $args = $_[0]." "."-ORBnameserviceport $nsmport ".
    "-ORBnameserviceior file://$iorfile";
  my $prog = $EXEPREFIX."client".$Process::EXE_EXT;

  $CL = Process::Create ($prog, $args);
}

# Options for all tests recognized by the 'client' program.
@opts = ("-s", "-t", "-i", "-e", "-y");

@comments = ("Simple Test: \n",
             "Tree Test: \n",
             "Iterator Test: \n",
             "Exceptions Test: \n",
             "Destroy Test: \n");

$test_number = 0;

# Run server and client with each option available to the client.
foreach $o (@opts)
{
  name_server ();

  print STDERR "\n";
  print STDERR "          ".$comments[$test_number];

  client ($o);
  $client = $CL->TimedWait (60);
  if ($client == -1) {
    print STDERR "ERROR: client timedout\n";
    $CL->Kill (); $CL->TimedWait (1);
  }


  $NS->Terminate (); $server = $NS->TimedWait (5);
  if ($server == -1) {
    print STDERR "ERROR: server timedout\n";
    $NS->Kill (); $NS->TimedWait (1);
  }
  $test_number++;
}

print STDERR "\n";

# Now run the multithreaded test, sending output to the file.
open (OLDOUT, ">&STDOUT");
open (STDOUT, ">test_run.data") or die "can't redirect stdout: $!";
open (OLDERR, ">&STDERR");
open (STDERR, ">&STDOUT") or die "can't redirect stderror: $!";

name_server ();
sleep $sleeptime;
client ("-m25");

$client = $CL->TimedWait (60);
if ($client == -1) {
  print STDERR "ERROR: client timedout\n";
  $CL->Kill (); $CL->TimedWait (1);
}


close (STDERR);
close (STDOUT);
open (STDOUT, ">&OLDOUT");
open (STDERR, ">&OLDERR");

$NS->Terminate (); $server = $NS->TimedWait (5);
if ($server == -1) {
  print STDERR "ERROR: server timedout\n";
  $NS->Kill (); $NS->TimedWait (1);
}

unlink $iorfile;

print STDERR "          Multithreaded Test:\n";
$FL = Process::Create ($EXEPREFIX."process-m-output.pl",
                       " test_run.data 25");
$filter = $FL->TimedWait (60);
if ($filter == -1) {
  print STDERR "ERROR: filter timedout\n";
  $FL->Kill (); $FL->TimedWait (1);
}
print STDERR "\n";

# @@ Capture any exit status from the processes.
exit 0;
