// $Id$

#define ACE_BUILD_SVC_DLL

#include "ace/Get_Opt.h"
#include "ace/Acceptor.h"
#include "ace/SOCK_Connector.h"
#include "ace/SOCK_Acceptor.h"
#include "ace/SPIPE_Acceptor.h"
#include "Client_Logging_Handler.h"
#include "ace/Log_Record.h"
#include "ace/OS_NS_stdio.h"
#include "ace/OS_NS_string.h"
#include "ace/OS_NS_sys_socket.h"
#include "ace/OS_NS_unistd.h"
#include "ace/CDR_Stream.h"

ACE_RCSID(lib, Client_Logging_Handler, "$Id$")

ACE_Client_Logging_Handler::ACE_Client_Logging_Handler (ACE_HANDLE output_handle)
  : logging_output_ (output_handle)
{
  // Register ourselves to receive SIGPIPE so we can attempt
  // reconnections.
#if !defined (ACE_LACKS_UNIX_SIGNALS)
  if (ACE_Reactor::instance ()->register_handler (SIGPIPE,
                                                  this) == -1)
    ACE_ERROR ((LM_ERROR,
                ACE_TEXT ("%n: %p\n"),
		ACE_TEXT ("register_handler (SIGPIPE)")));
#endif /* !ACE_LACKS_UNIX_SIGNALS */
}

// This is called when a <send> to the logging server fails...

int
ACE_Client_Logging_Handler::handle_signal (int signum,
                                           siginfo_t *,
                                           ucontext_t *)
{
  if (signum == SIGPIPE)
    return 0;
  else
    return -1;
}

// This function is called every time a client connects to us.

int
ACE_Client_Logging_Handler::open (void *)
{
  LOGGING_ADDR server_addr;

  // Register ourselves to receive <handle_input> callbacks when
  // clients send us logging records.  Note that since we're really a
  // Singleton, this->peer() will change after each connect, so we
  // need to grab the value now.
  if (ACE_Reactor::instance ()->register_handler
      (this->peer ().get_handle (),
       this,
       ACE_Event_Handler::READ_MASK
       | ACE_Event_Handler::EXCEPT_MASK) == -1)
    ACE_ERROR_RETURN ((LM_ERROR,
                       ACE_TEXT ("%n: %p\n"),
		       ACE_TEXT ("register_handler")),
                      -1);
  // Figure out what remote port we're really bound to.
  if (this->peer ().get_remote_addr (server_addr) == -1)
    ACE_ERROR_RETURN ((LM_ERROR,
                       ACE_TEXT ("%p\n"),
                       ACE_TEXT ("get_remote_addr")),
                      -1);
  ACE_DEBUG ((LM_DEBUG,
	      ACE_TEXT ("connected to client on handle %u\n"),
	      this->peer ().get_handle ()));
  return 0;
}

/* VIRTUAL */ ACE_HANDLE
ACE_Client_Logging_Handler::get_handle (void) const
{
  ACE_TRACE ("ACE_Client_Logging_Handler::get_handle");

  ACE_ERROR ((LM_ERROR,
	      ACE_TEXT ("get_handle() shouldn't be called\n")));

  return ACE_INVALID_HANDLE;
}

// Receive a logging record from an application.

int
ACE_Client_Logging_Handler::handle_input (ACE_HANDLE handle)
{
#if 0
  ACE_DEBUG ((LM_DEBUG,
              ACE_TEXT ("in handle_input, handle = %u\n"),
              handle));
#endif /* 0 */

  if (handle == this->logging_output_)
    // We're getting a message from the logging server!
    ACE_ERROR_RETURN ((LM_ERROR,
                       ACE_TEXT ("received data from server!\n")),
                      -1);
  ACE_Log_Record log_record;
#if defined (ACE_HAS_STREAM_PIPES)
  // @@ To Do
  // We're getting a logging message from a local application.

  ACE_Str_Buf msg ((void *) &log_record,
		   0,
		   sizeof log_record);

  ACE_SPIPE_Stream spipe;
  spipe.set_handle (handle);
  int flags = 0;

  int result = spipe.recv ((ACE_Str_Buf *) 0,
                           &msg,
                           &flags);

  // We've got a framed IPC mechanism, so we can just to a <recv>.
  if (result < 0 || msg.len == 0)
    {
      if (ACE_Reactor::instance ()->remove_handler
	  (handle,
	   ACE_Event_Handler::READ_MASK
	   | ACE_Event_Handler::EXCEPT_MASK
	   | ACE_Event_Handler::DONT_CALL) == -1)
	ACE_ERROR_RETURN ((LM_ERROR,
                           ACE_TEXT ("%n: %p\n"),
			   ACE_TEXT ("remove_handler")),
                          -1);
      spipe.close ();
#if 0
      ACE_DEBUG ((LM_DEBUG,
                  ACE_TEXT ("client closing down\n")));
#endif /* 0 */
      return 0;
    }
#else

  // We need to use the old two-read trick here since TCP sockets
  // don't support framing natively.  Allocate a message block for the
  // payload; initially at least large enough to hold the header, but
  // needs some room for alignment.
  ACE_Message_Block *payload = 0;
  ACE_Message_Block *header =
    new ACE_Message_Block (ACE_DEFAULT_CDR_BUFSIZE);
  // Align the Message Block for a CDR stream
  ACE_CDR::mb_align (header);

  ACE_CDR::Boolean byte_order;
  ACE_CDR::ULong length;

  ssize_t count = ACE::recv_n (handle,
			       header->wr_ptr (),
			       8);
  switch (count)
    {
      // Handle shutdown and error cases.
    default:
    case -1:
    case 0:
      if (ACE_Reactor::instance ()->remove_handler
            (handle,
              ACE_Event_Handler::READ_MASK
              | ACE_Event_Handler::EXCEPT_MASK
              | ACE_Event_Handler::DONT_CALL) == -1)
         ACE_ERROR_RETURN ((LM_ERROR,
                            ACE_TEXT ("%n: %p\n"),
                            ACE_TEXT ("remove_handler")),
                            0);
      if (handle == this->peer ().get_handle ())
        this->peer ().close ();
      else
        ACE_OS::closesocket (handle);
      // Release the memory to prevent a leak.
      header->release ();
      header = 0;
#if 0
      ACE_DEBUG ((LM_DEBUG,
                  ACE_TEXT ("client closing down\n")));
#endif
      return 0;
      /* NOTREACHED */

    case 8:
#if defined (ACE_WIN32)
      // This is a special-case sent from near line 610 in
      // Log_Msg.cpp.  Without this code Win32 sockets are never
      // closed, so this server will quickly run out of handles.
      if (length == ~0)
        {
          if (ACE_Reactor::instance ()->remove_handler
              (handle,
               ACE_Event_Handler::READ_MASK
               | ACE_Event_Handler::EXCEPT_MASK
               | ACE_Event_Handler::DONT_CALL) == -1)
            ACE_ERROR_RETURN ((LM_ERROR,
                               ACE_TEXT ("%n: %p\n"),
                               ACE_TEXT ("remove_handler")),
                              0);

          ACE_OS::closesocket (handle);
#  if 0
          ACE_DEBUG ((LM_DEBUG,
                      ACE_TEXT ("client closing down\n")));
#  endif /* 0 */
          return 0;
        }
#endif /* ACE_WIN32 */
      header->wr_ptr (8); // Reflect addition of 8 bytes.

      // Create a CDR stream to parse the 8-byte header.
      ACE_InputCDR cdr (header);

      // Extract the byte-order and use helper methods to disambiguate
      // octet, booleans, and chars.
      cdr >> ACE_InputCDR::to_boolean (byte_order);

      // Set the byte-order on the stream...
      cdr.reset_byte_order (byte_order);

      // Extract the length
      cdr >> length;

      payload =	new ACE_Message_Block (length);
      // Ensure there's sufficient room for log record payload.
      ACE_CDR::grow (payload, 8 + ACE_CDR::MAX_ALIGNMENT + length);

      // Use <recv_n> to obtain the contents.
      if (ACE::recv_n (handle, payload->wr_ptr (), length) > 0) 
        payload->wr_ptr (length);   // Reflect additional bytes
      else
        {
          ACE_ERROR ((LM_ERROR,
                      ACE_TEXT ("%p\n"),
                      ACE_TEXT ("recv")));

          if (ACE_Reactor::instance ()->remove_handler
              (handle,
               ACE_Event_Handler::READ_MASK
               | ACE_Event_Handler::EXCEPT_MASK
               | ACE_Event_Handler::DONT_CALL) == -1)
            ACE_ERROR ((LM_ERROR,
                        ACE_TEXT ("%n: %p\n"),
                        ACE_TEXT ("remove_handler")));

          ACE_OS::closesocket (handle);
          // Release the memory to prevent a leak.
          payload->release ();
          payload = 0;
	  header->release ();
	  header = 0;
          return 0;
        }
    }
#endif /* ACE_HAS_STREAM_PIPES */

  ACE_InputCDR cdr (payload);
  cdr.reset_byte_order (byte_order);
  cdr >> log_record;  // Finally extract the <ACE_log_record>.

  log_record.length (length);

  // Forward the logging record to the server.
  if (this->send (log_record) == -1)
    ACE_ERROR ((LM_ERROR,
		ACE_TEXT ("%p\n"),
		ACE_TEXT ("send")));
  // Release the memory to prevent a leak.
  payload->release ();
  payload = 0;
  header->release ();
  header = 0;
  return 0;
}

// Receive a logging record from an application send via a non-0
// MSG_BAND...  This just calls handle_input().

int
ACE_Client_Logging_Handler::handle_exception (ACE_HANDLE handle)
{
  return this->handle_input (handle);
}

// Called when object is removed from the ACE_Reactor

int
ACE_Client_Logging_Handler::close (u_long)
{
  if (this->logging_output_ != ACE_STDERR)
    ACE_OS::closesocket (this->logging_output_);

  this->destroy ();
  return 0;
}

int
ACE_Client_Logging_Handler::handle_output (ACE_HANDLE)
{
  return 0;
}

// Encodes the contents of log_record object using network byte-order
// and sends it to the logging server.

int
ACE_Client_Logging_Handler::send (ACE_Log_Record &log_record)
{
  ostream *orig_ostream = ACE_Log_Msg::instance ()->msg_ostream ();

  // This logic must occur before we do the encode() on <log_record>
  // since otherwise the values of the <log_record> fields will be in
  // network byte order.

  if (orig_ostream)
    log_record.print (ACE_TEXT ("<localhost>"),
                      ACE_Log_Msg::instance ()->flags (),
                      *orig_ostream);

  if (this->logging_output_ == ACE_STDERR)
    log_record.print (ACE_TEXT ("<localhost>"),
                      ACE_Log_Msg::instance ()->flags (),
                      stderr);
  else
    {
      // Serialize the log record using a CDR stream, allocate enough
      // space for the complete <ACE_Log_Record>.
      const size_t max_payload_size =
	4 // type()
	+ 8 // timestamp
	+ 4 // process id
	+ 4 // data length
	+ ACE_Log_Record::MAXLOGMSGLEN // data
	+ ACE_CDR::MAX_ALIGNMENT; // padding;

      // Insert contents of <log_record> into payload stream.
      ACE_OutputCDR payload (max_payload_size);
      payload << log_record;

      // Get the number of bytes used by the CDR stream.
      ACE_CDR::ULong length = payload.total_length ();

      // Send a header so the receiver can determine the byte order and
      // size of the incoming CDR stream.
      ACE_OutputCDR header (ACE_CDR::MAX_ALIGNMENT + 8);
      header << ACE_OutputCDR::from_boolean (ACE_CDR_BYTE_ORDER);

      // Store the size of the payload that follows
      header << ACE_CDR::ULong (length);

      // Use an iovec to send both buffer and payload simultaneously.
      iovec iov[2];
      iov[0].iov_base = header.begin ()->rd_ptr ();
      iov[0].iov_len  = 8;
      iov[1].iov_base = payload.begin ()->rd_ptr ();
      iov[1].iov_len  = length;

      // We're running over sockets, so send header and payload
      // efficiently using "gather-write".  
      if (ACE::sendv_n (this->logging_output_,iov, 2) == -1)
	{
	  ACE_DEBUG ((LM_DEBUG, "WILL: Something about the sending failed.  Switching to stderr\n"));
	  if (ACE_Log_Msg::instance ()->msg_ostream () == 0)
	    // Switch over to logging to stderr for now.  At some point,
	    // we'll improve the implementation to queue up the message,
	    // try to reestablish a connection, and then send the queued
	    // data once we've reconnect to the logging server.  If
	    // you'd like to implement this functionality and contribute
	    // it back to ACE that would be great!
	    this->logging_output_ = ACE_STDERR;
	}
      else
	{
	  ACE_DEBUG ((LM_DEBUG, "WILL: Sent logging message successfully!\n"));
	}
#if 0
      long len = log_record.length ();
      log_record.encode ();

      if (ACE::send (this->logging_output_,
		     (char *) &log_record,
		     len) == -1)
        if (ACE_Log_Msg::instance ()->msg_ostream () == 0)
          // Switch over to logging to stderr for now.  At some point,
          // we'll improve the implementation to queue up the message,
          // try to reestablish a connection, and then send the queued
          // data once we've reconnect to the logging server.  If
          // you'd like to implement this functionality and contribute
          // it back to ACE that would be great!
          this->logging_output_ = ACE_STDERR;
#endif 
    }

  return 0;
}

class ACE_Client_Logging_Acceptor : public ACE_Acceptor<ACE_Client_Logging_Handler, LOGGING_ACCEPTOR>
{
  // = TITLE
  //     This factory creates connections with the
  //     <Server_Logging_Acceptor>.
  //
  // = DESCRIPTION
  //     This class contains the service-specific methods that can't
  //     easily be factored into the <ACE_Acceptor>.
public:
  // = Initialization method.
  ACE_Client_Logging_Acceptor (void);
  // Default constructor.

protected:
  // = Dynamic linking hooks.
  virtual int init (int argc, ACE_TCHAR *argv[]);
  // Called when service is linked.

  virtual int fini (void);
  // Called when service is unlinked.

  virtual int info (ACE_TCHAR **strp, size_t length) const;
  // Called to determine info about the service.

  virtual int make_svc_handler (ACE_Client_Logging_Handler *&sh);
  // Factory that always returns the <handler_>.

  // = Scheduling hooks.
  virtual int suspend (void);
  virtual int resume (void);

private:
  int parse_args (int argc, ACE_TCHAR *argv[]);
  // Parse svc.conf arguments.

  const ACE_TCHAR *server_host_;
  // Host where the logging server is located.

  u_short server_port_;
  // Port number where the logging server is listening for
  // connections.

  ACE_INET_Addr server_addr_;
  // Address of the logging server.

  const ACE_TCHAR *logger_key_;
  // Communication endpoint where the client logging daemon will
  // listen for connections from clients.

  ACE_Client_Logging_Handler *handler_;
  // Pointer to the handler that does the work.
};

int
ACE_Client_Logging_Acceptor::fini (void)
{
  this->close ();

  if (this->handler_ != 0)
    this->handler_->close (0);

  // Try to unlink the logger key so weird things don't happen if
  // we're using STREAM pipes.
  ACE_OS::unlink (this->logger_key_);

  // This memory was allocated by <ACE_OS::strdup>.
  ACE_OS::free ((void *) this->logger_key_);
  ACE_OS::free ((void *) this->server_host_);

  return 0;
}

int
ACE_Client_Logging_Acceptor::make_svc_handler (ACE_Client_Logging_Handler *&sh)
{
  // Always return a pointer to the Singleton handler.
  sh = this->handler_;
  return 0;
}

int
ACE_Client_Logging_Acceptor::info (ACE_TCHAR **strp, size_t length) const
{
  ACE_TCHAR buf[BUFSIZ];

  ACE_OS::sprintf (buf, ACE_TEXT ("%d/%s %s"),
		   this->server_addr_.get_port_number (), "tcp",
		   "# client logging daemon\n");

  if (*strp == 0 && (*strp = ACE_OS::strdup (buf)) == 0)
    return -1;
  else
    ACE_OS::strncpy (*strp, buf, length);
  return ACE_OS::strlen (buf);
}

ACE_Client_Logging_Acceptor::ACE_Client_Logging_Acceptor (void)
  : server_host_ (ACE_OS::strdup (ACE_DEFAULT_SERVER_HOST)),
    server_port_ (ACE_DEFAULT_LOGGING_SERVER_PORT),
    logger_key_ (ACE_OS::strdup (ACE_DEFAULT_LOGGER_KEY)),
    handler_ (0)
{
}

int
ACE_Client_Logging_Acceptor::init (int argc, ACE_TCHAR *argv[])
{
  // We'll log *our* error and debug messages to stderr!
  if (ACE_LOG_MSG->open (ACE_TEXT ("Client Logging Service")) == -1)
    ACE_ERROR_RETURN ((LM_ERROR,
                       ACE_TEXT ("Can't open Log_Msg\n")),
                      -1);

  // Use the options hook to parse the command line arguments and set
  // options.
  this->parse_args (argc, argv);

  // Try to unlink the logger key so weird things don't happen if
  // we're using STREAM pipes.
  ACE_OS::unlink (this->logger_key_);

  // Initialize the acceptor endpoint.
  if (this->open (LOGGING_ADDR (this->logger_key_)) == -1)
    ACE_ERROR_RETURN ((LM_ERROR,
                       ACE_TEXT ("%p\n"),
                       this->logger_key_),
                      -1);

  // Establish connection with the server.
  ACE_SOCK_Connector con;
  ACE_SOCK_Stream stream;

  if (con.connect (stream,
                   this->server_addr_) == -1)
    {
      ACE_ERROR ((LM_ERROR,
                  ACE_TEXT ("%p, using stderr\n"),
		  ACE_TEXT ("can't connect to logging server")));
      if (ACE_Log_Msg::instance ()->msg_ostream () == 0)
        // If we can't connect to the server then we'll send the logging
        // messages to stderr.
        stream.set_handle (ACE_STDERR);
    }
  else
    {
      ACE_INET_Addr server_addr;

      // Figure out what remote port we're really bound to.
      if (stream.get_remote_addr (server_addr) == -1)
        ACE_ERROR_RETURN ((LM_ERROR,
                           ACE_TEXT ("%p\n"),
                           ACE_TEXT ("get_remote_addr")),
                          -1);
      ACE_DEBUG ((LM_DEBUG,
		  ACE_TEXT ("starting up Client Logging Daemon, ")
		  ACE_TEXT ("connected to port %d on handle %u\n"),
		  server_addr.get_port_number (),
		  stream.get_handle ()));
    }

  // Create the Singleton <Client_Logging_Handler>.
  ACE_NEW_RETURN (this->handler_,
		  ACE_Client_Logging_Handler (stream.get_handle ()),
		  -1);
  return 0;
}

int
ACE_Client_Logging_Acceptor::parse_args (int argc, ACE_TCHAR *argv[])
{
  ACE_Get_Opt get_opt (argc, argv, ACE_TEXT ("h:k:p:"), 0);

  for (int c; (c = get_opt ()) != -1; )
    {
      switch (c)
       {
        case 'h':
          ACE_OS::free ((void *) this->server_host_);
          this->server_host_ = ACE_OS::strdup (get_opt.opt_arg ());
          break;
        case 'k':
          ACE_OS::free ((void *) this->logger_key_);
          this->logger_key_ = ACE_OS::strdup (get_opt.opt_arg ());
          break;
        case 'p':
          this->server_port_ = ACE_OS::atoi (get_opt.opt_arg ());
          break;
        default:
          ACE_ERROR_RETURN ((LM_ERROR,
                                   ACE_TEXT ("%n:\n[-p server-port]\n%a"), 1),
                                  -1);
        }
    }

  if (this->server_addr_.set (this->server_port_,
			      this->server_host_) == -1)
    ACE_ERROR_RETURN ((LM_ERROR,
                       ACE_TEXT ("%p\n"),
                       ACE_TEXT ("set")),
                      -1);
  return 0;
}

int
ACE_Client_Logging_Acceptor::suspend (void)
{
  // To be done...
  return 0;
}

int
ACE_Client_Logging_Acceptor::resume (void)
{
  // To be done...
  return 0;
}

// The following is a "Factory" used by the ACE_Service_Config and
// svc.conf file to dynamically initialize the state of the
// single-threaded logging server.

ACE_SVC_FACTORY_DEFINE (ACE_Client_Logging_Acceptor)

