/*
 * This application is derived from the sample application "subscribe_publish_sample" made 
 * available with the AWS IoT C SDK. 
 * 
 * At the time of writing the AWS IoT C SDK could be downloaded from:
 * https://s3.amazonaws.com/aws-iot-device-sdk-embedded-c/linux_mqtt_openssl-latest.tar
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

#include <signal.h>
#include <memory.h>
#include <sys/time.h>
#include <limits.h>

#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_interface.h"
#include "aws_iot_config.h"


// ============================================================================
// Global variables
// ============================================================================

// Default cert location
char certDirectory[PATH_MAX + 1] = "../../certs";

// Default MQTT HOST URL is pulled from the aws_iot_config.h
char HostAddress[255] = AWS_IOT_MQTT_HOST;

// Default MQTT port is pulled from the aws_iot_config.h
uint32_t port = AWS_IOT_MQTT_PORT;


// ============================================================================
// Functions
// ============================================================================

// MQTT message received callback handler
int mqttMessageReceivedCallbackHandler(MQTTCallbackParams params) {
	INFO("Subscribe callback");
	INFO("%.*s\t%.*s",
			(int)params.TopicNameLen, params.pTopicName,
			(int)params.MessageParams.PayloadLen, (char*)params.MessageParams.pPayload);

	return 0;
}

// MQTT disconnect callback handler
void mqttDisconnectCallbackHandler(void) {
	WARN("MQTT Disconnect");

	IoT_Error_t rc = NONE_ERROR;

	if (aws_iot_is_autoreconnect_enabled()) {
		INFO("Auto Reconnect is enabled, Reconnecting attempt will start now");
	} else {
		WARN("Auto Reconnect not enabled. Starting manual reconnect...");

		rc = aws_iot_mqtt_attempt_reconnect();

		if (RECONNECT_SUCCESSFUL == rc) {
			WARN("Manual Reconnect Successful");
		} else {
			WARN("Manual Reconnect Failed - %d", rc);
		}
	}
}

// Parse the command line arguments containing connection details
void parseInputArgsForConnectParams(int argc, char** argv) {
	int opt;

	while (-1 != (opt = getopt(argc, argv, "h:p:c:"))) {
		switch (opt) {
		case 'h':
			strcpy(HostAddress, optarg);
			DEBUG("Host %s", optarg);
			break;
		case 'p':
			port = atoi(optarg);
			DEBUG("arg %s", optarg);
			break;
		case 'c':
			strcpy(certDirectory, optarg);
			DEBUG("cert root directory %s", optarg);
			break;
		case '?':
			if (optopt == 'c') {
				ERROR("Option -%c requires an argument.", optopt);
			} else if (isprint(optopt)) {
				WARN("Unknown option `-%c'.", optopt);
			} else {
				WARN("Unknown option character `\\x%x'.", optopt);
			}
			break;
		default:
			ERROR("Error in command line argument parsing");
			break;
		}
	}
}


// ============================================================================
// Main function
// ============================================================================

int main(int argc, char** argv) {
	IoT_Error_t rc = NONE_ERROR;

	char rootCA[PATH_MAX + 1];
	char clientCRT[PATH_MAX + 1];
	char clientKey[PATH_MAX + 1];
	char CurrentWD[PATH_MAX + 1];
	char cafileName[] = AWS_IOT_ROOT_CA_FILENAME;
	char clientCRTName[] = AWS_IOT_CERTIFICATE_FILENAME;
	char clientKeyName[] = AWS_IOT_PRIVATE_KEY_FILENAME;

    char currStdinChar = '\0';

	parseInputArgsForConnectParams(argc, argv);

	INFO("\nAWS IoT SDK Version %d.%d.%d-%s\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    // Print path to certificates
	getcwd(CurrentWD, sizeof(CurrentWD));
	sprintf(rootCA, "%s/%s/%s", CurrentWD, certDirectory, cafileName);
	sprintf(clientCRT, "%s/%s/%s", CurrentWD, certDirectory, clientCRTName);
	sprintf(clientKey, "%s/%s/%s", CurrentWD, certDirectory, clientKeyName);

	DEBUG("rootCA %s", rootCA);
	DEBUG("clientCRT %s", clientCRT);
	DEBUG("clientKey %s", clientKey);

    // Set MQTT connection parameters
	MQTTConnectParams connectParams = MQTTConnectParamsDefault;

	connectParams.KeepAliveInterval_sec = 10;
	connectParams.isCleansession = true;
	connectParams.MQTTVersion = MQTT_3_1_1;
	connectParams.pClientID = "receive-random-numbers-sample-application";
	connectParams.pHostURL = HostAddress;
	connectParams.port = port;
	connectParams.isWillMsgPresent = false;
	connectParams.pRootCALocation = rootCA;
	connectParams.pDeviceCertLocation = clientCRT;
	connectParams.pDevicePrivateKeyLocation = clientKey;
	connectParams.mqttCommandTimeout_ms = 2000;
	connectParams.tlsHandshakeTimeout_ms = 5000;
	connectParams.isSSLHostnameVerify = true; // ensure this is set to true for production
	connectParams.disconnectHandler = mqttDisconnectCallbackHandler;

    // Connect to message broker via MQTT protocol
	INFO("Connecting...");

	rc = aws_iot_mqtt_connect(&connectParams);

	if (NONE_ERROR != rc) {
		ERROR("Error(%d) connecting to %s:%d", rc, connectParams.pHostURL, connectParams.port);
	}

	rc = aws_iot_mqtt_autoreconnect_set_status(true);

	if (NONE_ERROR != rc) {
		ERROR("Unable to set Auto Reconnect to true - %d", rc);

		return rc;
	}

    // Subscribe to MQTT topic
	MQTTSubscribeParams subParams = MQTTSubscribeParamsDefault;
	subParams.mHandler = mqttMessageReceivedCallbackHandler;
	subParams.pTopic = "sample-application/random-number";
	subParams.qos = QOS_0;

	if (NONE_ERROR == rc) {
		INFO("Subscribing...");

		rc = aws_iot_mqtt_subscribe(&subParams);

		if (NONE_ERROR != rc) {
			ERROR("Error subscribing");
		}
	}

    // Wait for MQTT messages to be received until "q" is entered or a connectivity error occurs
    INFO("Waiting for MQTT messages until a line is entered at standard input...");

    while ((NETWORK_ATTEMPTING_RECONNECT == rc || RECONNECT_SUCCESSFUL == rc || NONE_ERROR == rc) 
            && (currStdinChar != 'q')) {
        // Wait for 1 second
        INFO("-->sleep");
        sleep(1);

        // Yield current thread to the MQTT client
        rc = aws_iot_mqtt_yield(100);

        // Ask user if we should exit
        INFO("Enter \"q\" to exit or any other character to continue to receive messages.");

        currStdinChar = getchar();
    }
    
    // Ensure no errors occurred while receiving messages
    if (NONE_ERROR != rc) {
        ERROR("An error occurred in the loop.\n");
    } else {
        INFO("Successfully received messages.\n");
    }

	return rc;
}

