// $Id: OS_NS_stropts.cpp 85460 2009-05-29 13:38:50Z msmit $

#include "ace/OS_NS_stropts.h"

ACE_RCSID(ace, OS_NS_stropts, "$Id: OS_NS_stropts.cpp 85460 2009-05-29 13:38:50Z msmit $")

#if !defined (ACE_HAS_INLINED_OSCALLS)
# include "ace/OS_NS_stropts.inl"
#endif /* ACE_HAS_INLINED_OSCALLS */

ACE_BEGIN_VERSIONED_NAMESPACE_DECL

#if !defined (ACE_LACKS_STROPTS_H)

int
ACE_OS::ioctl (ACE_HANDLE socket,
               unsigned long io_control_code,
               void *in_buffer_p,
               unsigned long in_buffer,
               void *out_buffer_p,
               unsigned long out_buffer,
               unsigned long *bytes_returned,
               ACE_OVERLAPPED *overlapped,
               ACE_OVERLAPPED_COMPLETION_FUNC func)
{
# if defined (ACE_HAS_WINSOCK2) && (ACE_HAS_WINSOCK2 != 0)
  ACE_SOCKCALL_RETURN (::WSAIoctl ((ACE_SOCKET) socket,
                                   io_control_code,
                                   in_buffer_p,
                                   in_buffer,
                                   out_buffer_p,
                                   out_buffer,
                                   bytes_returned,
                                   (WSAOVERLAPPED *) overlapped,
                                   func),
                       int,
                       SOCKET_ERROR);
# else
  ACE_UNUSED_ARG (socket);
  ACE_UNUSED_ARG (io_control_code);
  ACE_UNUSED_ARG (in_buffer_p);
  ACE_UNUSED_ARG (in_buffer);
  ACE_UNUSED_ARG (out_buffer_p);
  ACE_UNUSED_ARG (out_buffer);
  ACE_UNUSED_ARG (bytes_returned);
  ACE_UNUSED_ARG (overlapped);
  ACE_UNUSED_ARG (func);
  ACE_NOTSUP_RETURN (-1);
# endif /* ACE_HAS_WINSOCK2 */
}

int
ACE_OS::ioctl (ACE_HANDLE socket,
               unsigned long io_control_code,
               ACE_QoS &ace_qos,
               unsigned long *bytes_returned,
               void *buffer_p,
               unsigned long buffer,
               ACE_OVERLAPPED *overlapped,
               ACE_OVERLAPPED_COMPLETION_FUNC func)
{
# if defined (ACE_HAS_WINSOCK2) && (ACE_HAS_WINSOCK2 != 0)

  QOS qos;
  unsigned long qos_len = sizeof (QOS);

  if (io_control_code == SIO_SET_QOS)
    {
      qos.SendingFlowspec = *(ace_qos.sending_flowspec ());
      qos.ReceivingFlowspec = *(ace_qos.receiving_flowspec ());
      qos.ProviderSpecific = (WSABUF) ace_qos.provider_specific ();

      qos_len += ace_qos.provider_specific ().iov_len;

      ACE_SOCKCALL_RETURN (::WSAIoctl ((ACE_SOCKET) socket,
                                       io_control_code,
                                       &qos,
                                       qos_len,
                                       buffer_p,
                                       buffer,
                                       bytes_returned,
                                       (WSAOVERLAPPED *) overlapped,
                                       func),
                           int,
                           SOCKET_ERROR);
    }
  else
    {
      unsigned long dwBufferLen = 0;

      int result = ::WSAIoctl ((ACE_SOCKET) socket,
                                io_control_code,
                                0,
                                0,
                                &dwBufferLen,
                                sizeof (dwBufferLen),
                                bytes_returned,
                                0,
                                0);


      if (result == SOCKET_ERROR)
        {
          unsigned long dwErr = ::WSAGetLastError ();

          if (dwErr == WSAEWOULDBLOCK)
            {
              errno = dwErr;
              return -1;
            }
          else
            if (dwErr != WSAENOBUFS)
              {
                errno = dwErr;
                return -1;
              }
          }

    char *qos_buf = 0;
    ACE_NEW_RETURN (qos_buf,
                    char [dwBufferLen],
                    -1);

    QOS *qos_ptr = reinterpret_cast<QOS*> (qos_buf);

    result = ::WSAIoctl ((ACE_SOCKET) socket,
                       io_control_code,
                       0,
                       0,
                       qos_ptr,
                       dwBufferLen,
                       bytes_returned,
                       0,
                       0);

    if (result == SOCKET_ERROR)
      return result;

    ACE_Flow_Spec sending_flowspec (qos_ptr->SendingFlowspec.TokenRate,
                                    qos_ptr->SendingFlowspec.TokenBucketSize,
                                    qos_ptr->SendingFlowspec.PeakBandwidth,
                                    qos_ptr->SendingFlowspec.Latency,
                                    qos_ptr->SendingFlowspec.DelayVariation,
#  if defined(ACE_HAS_WINSOCK2_GQOS)
                                    qos_ptr->SendingFlowspec.ServiceType,
                                    qos_ptr->SendingFlowspec.MaxSduSize,
                                    qos_ptr->SendingFlowspec.MinimumPolicedSize,
#  else /* ACE_HAS_WINSOCK2_GQOS */
                                    0,
                                    0,
                                    0,
#  endif /* ACE_HAS_WINSOCK2_GQOS */
                                    0,
                                    0);

    ACE_Flow_Spec receiving_flowspec (qos_ptr->ReceivingFlowspec.TokenRate,
                                      qos_ptr->ReceivingFlowspec.TokenBucketSize,
                                      qos_ptr->ReceivingFlowspec.PeakBandwidth,
                                      qos_ptr->ReceivingFlowspec.Latency,
                                      qos_ptr->ReceivingFlowspec.DelayVariation,
#  if defined(ACE_HAS_WINSOCK2_GQOS)
                                      qos_ptr->ReceivingFlowspec.ServiceType,
                                      qos_ptr->ReceivingFlowspec.MaxSduSize,
                                      qos_ptr->ReceivingFlowspec.MinimumPolicedSize,
#  else /* ACE_HAS_WINSOCK2_GQOS */
                                      0,
                                      0,
                                      0,
#  endif /* ACE_HAS_WINSOCK2_GQOS */
                                      0,
                                      0);

       ace_qos.sending_flowspec (&sending_flowspec);
       ace_qos.receiving_flowspec (&receiving_flowspec);
       ace_qos.provider_specific (*((struct iovec *) (&qos_ptr->ProviderSpecific)));


      return result;
    }

# else
  ACE_UNUSED_ARG (socket);
  ACE_UNUSED_ARG (io_control_code);
  ACE_UNUSED_ARG (ace_qos);
  ACE_UNUSED_ARG (bytes_returned);
  ACE_UNUSED_ARG (buffer_p);
  ACE_UNUSED_ARG (buffer);
  ACE_UNUSED_ARG (overlapped);
  ACE_UNUSED_ARG (func);
  ACE_NOTSUP_RETURN (-1);
# endif /* ACE_HAS_WINSOCK2 */
}

#endif /* !ACE_LACKS_STROPTS_H */

ACE_END_VERSIONED_NAMESPACE_DECL
