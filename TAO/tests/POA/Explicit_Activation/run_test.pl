#$Id$
# -*- perl -*-
eval '(exit $?0)' && eval 'exec perl -S $0 ${1+"$@"}'
    & eval 'exec perl -S $0 $argv:q'
    if 0;

unshift @INC, '../../../../bin';
require ACEutils;

$iorfile = "ior";

$oneway = "";
$iterations = 100;

# Parse the arguments
for ($i = 0; $i <= $#ARGV; $i++)
{
  SWITCH:
  {
    if ($ARGV[$i] eq "-h" || $ARGV[$i] eq "-?")
    {
      print "run_test [-h] [-i iterations] [-o] [-f ior file]\n";
      print "\n";
      print "-h                  -- prints this information\n";
      print "-f                  -- ior file\n";
      print "-i iterations       -- specifies iterations\n";
      print "-o                  -- call issued are oneways\n";
      exit;
    }
    if ($ARGV[$i] eq "-o")
    {
      $oneway = "-o";
      last SWITCH;
    }
    if ($ARGV[$i] eq "-i")
    {
      $iterations = $ARGV[$i + 1];
      $i++;
      last SWITCH;
    }
    if ($ARGV[$i] eq "-f")
    {
      $iorfile = $ARGV[$i + 1];
      $i++;
      last SWITCH;
    }
    print "run_test: Unknown Option: ".$ARGV[$i]."\n";
  }
}

$iorfile_1 = $iorfile."_1";
$iorfile_2 = $iorfile."_2";
$iorfile_3 = $iorfile."_3";

$SV = Process::Create ("server$Process::EXE_EXT", "-f $iorfile");

ACE::waitforfile ($iorfile_1);
ACE::waitforfile ($iorfile_2);
ACE::waitforfile ($iorfile_3);

$status  = system ("../Generic_Servant/client$Process::EXE_EXT $oneway -i $iterations -f $iorfile_1");
$status  = system ("../Generic_Servant/client$Process::EXE_EXT $oneway -i $iterations -f $iorfile_2");
$status  = system ("../Generic_Servant/client$Process::EXE_EXT $oneway -i $iterations -f $iorfile_3 -x");

unlink $iorfile_1;
unlink $iorfile_2;
unlink $iorfile_3;

exit $status;
