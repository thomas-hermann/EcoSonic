#ifndef OSCSENDER_H
#define OSCSENDER_H

#include <osc/OscOutboundPacketStream.h>
#include <ip/UdpSocket.h>

class OSCSender {
public:
    OSCSender()
        : transmitSocket(IpEndpointName( "127.0.0.1", 57120))
        , buffer(1024)
    { }
    void send_float(const char* msg, double val)
    {
        //qDebug() << "osc:" << msg << val;
        osc::OutboundPacketStream p(&buffer[0], buffer.size());
        p << osc::BeginBundleImmediate
            << osc::BeginMessage( msg )
                << val << osc::EndMessage
            << osc::EndBundle;
        transmitSocket.Send(p.Data(), p.Size());
    }
    void call(const char* msg) {
        qDebug() << "osc:" << msg;
        send_float(msg, 0);
    }

protected:
    UdpTransmitSocket transmitSocket;
    std::vector<char> buffer;
};

#endif // OSCSENDER_H
