
//=============================================================================
/**
 *  @file   TAO_Service.cpp
 *
 * This directory contains an example that illustrates how the ACE
 * Service Configurator can dynamically configure an ORB and its
 * servants from a svc.conf file.
 *
 *  @author  Priyanka Gontla <pgontla@ece.uci.edu>
 */
//=============================================================================


#include "orbsvcs/Log_Macros.h"
#include "tao/ORB_Constants.h"
#include "tao/ORB.h"
#include "ace/Service_Config.h"
#include "orbsvcs/Log_Macros.h"
#include "ace/Signal.h"
#include "ace/Time_Value.h"
#include "ace/Argv_Type_Converter.h"

extern "C" void handler (int)
{
  ACE_Service_Config::reconfig_occurred (1);
}

int
ACE_TMAIN (int argc, ACE_TCHAR *argv[])
{
  try
    {
      // ORB initialization boiler plate...
      CORBA::ORB_var orb = CORBA::ORB_init (argc, argv);

      ACE_Sig_Action sa ((ACE_SignalHandler) handler, SIGHUP);

      for (;;)
        {
          ACE_Time_Value tv (5, 0);

          orb->perform_work (tv);

          ORBSVCS_DEBUG ((LM_DEBUG,
                      "Reconfig flag = %d\n",
                      ACE_Service_Config::reconfig_occurred ()));

          if (ACE_Service_Config::reconfig_occurred ())
            ACE_Service_Config::reconfigure ();
        }

    }
  catch (const CORBA::Exception& ex)
    {
      ex._tao_print_exception (argv[0]);
    }

  return -1;
}
