#include <iostream>

/*
 * DEVICE <-> Gateway
 *
 * -> mqtt.connect,host,port,will.topic,will.message,will.qos,will.retain,clientId,user,password
 * <- mqtt.connack,error
 *
 * -> mqtt.publish,topic,message,qos,retain
 * <- mqtt.pubcomp
 *
 * -> mqtt.subscribe
 * <- mqtt.suback
 *
 * -> mqtt.unsubscribe
 * <- mqtt.unsuback
 *
 * -> mqtt.pingreq
 * <- mqtt.pingresp
 *
 * -> mqtt.disconnect
 */

using namespace std;
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <stdint.h>
#include <string.h>
#include <cstdlib>

#include "Str.h"
#include <time.h>
//#include "Timer.h"
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
//#include "Sequence.h"
#include <unistd.h>
//#include "Thread.h"
// #include "Sequence.h"
#include "Tcp.h"
#include "Usb.h"
#include <time.h>
#include <Actor.h>
#define TIMER_TICK 1000

#include <Log.h>
#include <CborQueue.h>
#include <SlipStream.h>
#include <EventBus.h>

struct {
	const char* host;
	uint16_t port;
	uint32_t baudrate;
	const char* device;
	LogManager::LogLevel logLevel;
} context = { "limero.ddns.net", 1883, 115200, "/dev/ttyACM0",
		LogManager::LOG_DEBUG };

Usb usb("/dev/ttyACM0");
Tcp tcp("localhost", 1883);

//_______________________________________________________________________________________
//
// simulates RTOS generating events into queue : Timer::TICK,Usb::RXD,Usb::CONNECTED,...
//_______________________________________________________________________________________

void poller(int usbFd, int tcpFd, uint64_t sleepTill) {
	Cbor cbor(100);
	Bytes bytes(1024);
	uint8_t buffer[1024];
	fd_set rfds;
	fd_set wfds;
	fd_set efds;
	struct timeval tv;
	int retval;
	uint64_t start = Sys::millis();
//    uint64_t delta=1000;
	if (usbFd == 0 && tcpFd == 0) {
		sleep(1);
		eb.publish(H("sys.tick"));
	} else {

		// Watch usbFd and tcpFd  to see when it has input.
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_ZERO(&efds);
		if (usbFd)
			FD_SET(usbFd, &rfds);
		if (tcpFd)
			FD_SET(tcpFd, &rfds);
		if (usbFd)
			FD_SET(usbFd, &efds);
		if (tcpFd)
			FD_SET(tcpFd, &efds);

		// Wait up to 1000 msec.
		uint64_t delta = 1000;
		if (sleepTill > Sys::millis()) {
			delta = sleepTill - Sys::millis();
		}

		tv.tv_sec = delta / 1000;
		tv.tv_usec = (delta * 1000) % 1000000;

		int maxFd = usbFd < tcpFd ? tcpFd : usbFd;
		maxFd += 1;

		start = Sys::millis();

		retval = select(maxFd, &rfds, NULL, &efds, &tv);

		if (retval < 0) {
			LOGF(" select() : %d %s", retval, strerror(retval));
			sleep(1);
		} else if (retval > 0) // one of the fd was set
				{
			if (FD_ISSET(usbFd,&rfds)) {
				int size = ::read(usbFd, buffer, sizeof(buffer));
				if (size >= 0) {
					for (int i = 0; i < size; i++)
						bytes.write(buffer[i]);
					cbor.addKey(H("data")).add(bytes);
					eb.publish(H("usb.rxd"), cbor);
				}
			}
			if (FD_ISSET(tcpFd,&rfds)) {
				eb.publish(H("tcp.rxd"));
			}
			if (FD_ISSET(usbFd,&efds)) {
				eb.publish(H("usb.err"));
			}
			if (FD_ISSET(tcpFd,&efds)) {
				eb.publish(H("tcp.err"));
			}
		} else {
			//TODO publish TIMER_TICK
			eb.publish(H("sys.tick"));
		}
	}
	uint64_t waitTime = Sys::millis() - start;
	if (waitTime > 1) {
//        LOGF(" waited %d/%d msec.",waitTime,delta);
	}
}

/*_______________________________________________________________________________

 loadOptions  role :
 - parse commandline otions
 h : host of mqtt server
 p : port
 d : the serial device "/dev/ttyACM*"
 b : the baudrate set ( only usefull for a usb2serial box or a real serial port )
 ________________________________________________________________________________*/

#include "Tcp.h"
#include "Log.h"

void loadOptions(int argc, char* argv[]) {
	int c;
	while ((c = getopt(argc, argv, "h:p:d:b:l:")) != -1)
		switch (c) {
		case 'h':
			context.host = optarg;
			break;
		case 'p':
			context.port = atoi(optarg);
			break;
		case 'd':
			context.device = optarg;
			break;
		case 'b':
			context.baudrate = atoi(optarg);
			break;
		case 'l':
			if (strcmp(optarg, "DEBUG") == 0)
				context.logLevel = LogManager::LOG_DEBUG;
			if (strcmp(optarg, "INFO") == 0)
				context.logLevel = LogManager::INFO;
			if (strcmp(optarg, "WARN") == 0)
				context.logLevel = LogManager::WARN;
			if (strcmp(optarg, "ERROR") == 0)
				context.logLevel = LogManager::ERROR;
			if (strcmp(optarg, "FATAL") == 0)
				context.logLevel = LogManager::FATAL;
			break;
		case '?':
			if (optopt == 'c')
				fprintf(stderr, "Option -%c requires an argument.\n", optopt);
			else if (isprint(optopt))
				fprintf(stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
			return;
		default:
			abort();
			break;
		}
}

#include <signal.h>
#include <execinfo.h>

void SignalHandler(int signal_number) {
	void *array[10];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);

	// print out all the frames to stderr
	fprintf(stderr, "Error: signal %d:%s \n", signal_number,
			strsignal(signal_number));
	backtrace_symbols_fd(array, size, STDERR_FILENO);
	exit(1);
}

void interceptAllSignals() {
	signal(SIGFPE, SignalHandler);
	signal(SIGILL, SignalHandler);
	signal(SIGINT, SignalHandler);
	signal(SIGSEGV, SignalHandler);
	signal(SIGTERM, SignalHandler);
}

SlipStream slip(256, usb);
#include "MqttClient.h"

void onUsbRxd(Cbor& cbor) {
	Bytes data(1000);
	if (cbor.gotoKey(H("data"))) {
		if (cbor.get(data)) {
//           LOGF(" data length : %d ",data.length());
			data.offset(0);
			while (data.hasData()) {
				slip.onRecv(data.read());
			}
		} else LOGF(" no data ");
	} else LOGF(" no key data ");
}

extern void logCbor(Cbor&);

void onStm32Ping(Cbor& cbor) {
	Str str("true");
	Str topic("stm32/system/alive");
	MqttClient::publish(topic, str);
}

void onMqttConnected(Cbor& cbor) {
	Str topic("put/stm32/#");
	MqttClient::subscribe(topic, 1);
}

int main(int argc, char *argv[]) {

	LOGF("Start %s version : %s %s", argv[0], __DATE__, __TIME__);

	loadOptions(argc, argv);
	if (context.logLevel <= LogManager::LOG_DEBUG) {
		eb.debug(true);
	};
	Str topic("stm32/system/alive"), value("false");
	Str connection(100);
	connection.append("tcp://").append(context.host).append(":").append(
			context.port); //limero.ddns.net:1883"
	MqttClient::connect(connection, topic, value, 1);

	interceptAllSignals();

	usb.setDevice(context.device);
	usb.setBaudrate(context.baudrate);
	tcp.setHost(context.host);
	tcp.setPort(context.port);
	Erc erc = usb.open();
	LOGF(" usb.open() : %d %s", erc, strerror(erc));

	eb.subscribe(H("usb.rxd"), [](Cbor& cbor) {
		Bytes data(1000);
		if (cbor.gotoKey(H("data"))) {
			if (cbor.get(data)) {
//           LOGF(" data length : %d ",data.length());
			data.offset(0);
			while (data.hasData()) {
				slip.onRecv(data.read());
			}
		} else LOGF(" no data ");
	} else LOGF(" no key data ");});

	eb.subscribe(H("mqtt.publish"),
			[](Cbor& cbor) {
				Str topic(100);
				Bytes message(100);
				if ( cbor.getKeyValue(H("topic"),topic) && cbor.getKeyValue(H("message"),message)) {
					int qos = 0;
					bool retain = false;
					cbor.getKeyValue(H("qos"), qos);
					cbor.getKeyValue(H("retain"), retain);
					MqttClient::publish(topic, message, qos, retain);
				} else LOGF(" bad mqtt.publish command ");
			});

	eb.subscribe(H("mqtt.subscribe"), [](Cbor& cbor) {
		Str topic(100);
		if (cbor.getKeyValue(H("topic"), topic)) {
			int qos = 0;
			cbor.getKeyValue(H("qos"), qos);
			MqttClient::subscribe(topic, qos);
		} else LOGF(" bad mqtt.subscribe command ");
	});

	eb.subscribe(0, [](Cbor& cbor) { // all events
				int cmd;
				if ( cbor.getKeyValue(0,cmd) ) {
					if ( cmd==H("mqtt.connack") || cmd==H("mqtt.suback")|| cmd==H("mqtt.published")|| cmd==H("mqtt.connected")) {
						slip.send(cbor);
					}
				}
			});

//    UsbConnection usbConnection(&usb);
//	Actor::initAll();
	eb.initAll();
	while (1) {
		poller(usb.fd(), tcp.fd(), 1000);
		eb.eventLoop();
	}

}
