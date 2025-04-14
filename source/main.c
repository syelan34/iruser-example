#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <malloc.h>

// disable ir:rst since it overrides ir:user
bool hidShouldUseIrrst(void) { return false; }

#define PACKET_INFO_SIZE 8
#define MAX_PACKET_SIZE 32
#define PACKET_COUNT 1
#define PACKET_BUFFER_SIZE PACKET_COUNT * (PACKET_INFO_SIZE + MAX_PACKET_SIZE)
#define CPP_CONNECTION_POLLING_PERIOD_MS 0x08
#define CPP_POLLING_PERIOD_MS 0x32
#define CPP_DEVICE_ID 1

static u32* iruserSharedMem;
static u32 iruserSharedMemSize;
static Handle connectionStatusEvent;
static Handle receivePacketEvent;


size_t round_up(size_t value, size_t multiple) {
    return (value / multiple + 1) * multiple;
}

void waitForInput(u32 key) {
    while(1) {
		gspWaitForVBlank();
		hidScanInput();
		u32 kDown = hidKeysDown();
		if (kDown & key) return;
	}
}

IRUSER_ConnectionStatus checkConnectionStatus() {
    IRUSER_StatusInfo statusInfo = iruserGetStatusInfo();
    
    return statusInfo.connection_status;
}

void printStatusInfo() {
    IRUSER_StatusInfo statusInfo = iruserGetStatusInfo();
    
    printf("recv err result: %08lx\n", statusInfo.recv_err_result);
    printf("send err result: %08lx\n", statusInfo.send_err_result);
    printf("connection status: %u\n", statusInfo.connection_status);
    printf("trying to connect status: %02x\n", statusInfo.trying_to_connect_status);
    printf("connection role: %02x\n", statusInfo.connection_role);
    printf("machine id: %02x\n", statusInfo.machine_id);
    printf("target machine id: %02x\n", statusInfo.target_machine_id);
    printf("network id: %02x\n", statusInfo.network_id);
    printf("unknown field 2: %02x\n", statusInfo.unknown_field_2);
    printf("unknown field 3: %02x\n", statusInfo.unknown_field_3);
}

#define SYNC_WAIT_TIMEOUT 100000000 // 100ms

bool attemptConnectCirclePadPro() {
    // only errors if already connected/connecting
    Result res = IRUSER_RequireConnection(CPP_DEVICE_ID);
    if (R_FAILED(res)) {printf("IRUSER_RequireConnection() failed: %08x\n", (unsigned int)res);return false;}
    // wait for connection status to change
    res = svcWaitSynchronization(connectionStatusEvent, SYNC_WAIT_TIMEOUT); // wait for 100ms for a response
    if (R_FAILED(res) && R_DESCRIPTION(res) != RD_TIMEOUT) {
        printf("svcWaitSynchronization() failed: %08x\n", (unsigned int)res);
        return res;
    }
    
    // check if connected
    if (checkConnectionStatus() == CNSTATUS_Connected) {
        printf("Circle Pad Pro connected successfully.\n");
        return true;
    }
    res = IRUSER_Disconnect();
    if (R_FAILED(res)) {printf("IRUSER_Disconnect() failed: %08x\n", (unsigned int)res);return false;}
    // wait for connection status to change
    res = svcWaitSynchronization(connectionStatusEvent, SYNC_WAIT_TIMEOUT); // wait for 100ms for a response
    if (R_FAILED(res) && R_DESCRIPTION(res) != RD_TIMEOUT) {
        printf("svcWaitSynchronization() failed: %08x\n", (unsigned int)res);
        return false;
    }
    return false;
}

Result iruser_test()
{
	Result ret=0;
	
	printf("IR:USER initialized successfully, looking for Circle Pad Pro...\n");

	tryconnect:
	printf("Press B to cancel.\n");
	while (!attemptConnectCirclePadPro()) {
		gspWaitForVBlank();
		hidScanInput();
		u32 kDown = hidKeysDown();
		
		if (kDown & KEY_B) return MAKERESULT(RL_SUCCESS, RS_CANCELED, RM_COMMON, RD_CANCEL_REQUESTED);
	}
	
	// Circle Pad Pro connected, start polling
	ret=iruserCPPRequestInputPolling(CPP_CONNECTION_POLLING_PERIOD_MS);
	
	if (R_FAILED(ret)) {printf("iruserCPPRequestInputPolling() failed: %08x\n", (unsigned int)ret);return ret;}

	printf("Press B to exit.\n");
	printf("Press A to display connection status or X to display connection role.\n");

	u32 kDown;
	circlePadProInputResponse state;
	while(1)
	{
		gspWaitForVBlank();
		hidScanInput();

		kDown = hidKeysDown();

		if(kDown & KEY_B) break;
		if (kDown & KEY_A) {
            IRUSER_ConnectionStatus status;
            ret = IRUSER_GetConnectionStatus(&status);
            if (R_FAILED(ret)) {printf("IRUSER_GetConnectionStatus() failed: %08x\n", (unsigned int)ret);return ret;}
            printf("Connection status is %u\n", status);
        }
		if (kDown & KEY_X) {
            IRUSER_ConnectionRole role;
            ret = IRUSER_GetConnectionRole(&role);
            if (R_FAILED(ret)) {printf("IRUSER_GetConnectionRole() failed: %08x\n", (unsigned int)ret);return ret;}
            printf("Connection role is %u.\n", role);
        }

		if (checkConnectionStatus() == CNSTATUS_Connected) ret = iruserGetCirclePadProState(&state);
		
		if (checkConnectionStatus() == CNSTATUS_Disconnected) {
		    printf("Disconnected from Circle Pad Pro.\n");
			printf("Attempting to reconnect...\n");
			goto tryconnect;
		}
		
		printf("\e[s\e[30;1Hdx:%04x dy:%04x bat:%04x btn:%03x unk:%02x\e[u", state.cstick.csPos.dx, state.cstick.csPos.dy, state.status.battery_level, state.status_raw >> 5, state.unknown_field);
	}

	return ret;
}


int main()
{
	Result ret=0;

	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	printf("iruser example.\n");
	
	iruserSharedMemSize = round_up(0x30 + PACKET_BUFFER_SIZE + PACKET_BUFFER_SIZE, 0x1000);
    iruserSharedMem = (u32*)memalign(iruserSharedMemSize, 0x1000);

	if(R_FAILED(ret = iruserInit(iruserSharedMem, iruserSharedMemSize, PACKET_BUFFER_SIZE, PACKET_COUNT))) {printf("iruserInit() failed: 0x%08x.\n", (unsigned int)ret);goto exit;}
	else if(R_FAILED(ret = IRUSER_GetConnectionStatusEvent(&connectionStatusEvent))) {printf("IRUSER_GetConnectionStatusEvent() failed: 0x%08x.\n", (unsigned int)ret);goto exit;}
	else if(R_FAILED(ret = IRUSER_GetReceiveEvent(&receivePacketEvent))) {printf("IRUSER_GetReceiveEvent() failed: 0x%08x.\n", (unsigned int)ret);goto exit;}


    hidInit();
	ret = iruser_test();
	printf("iruser_test() returned: 0x%08x.\n", (unsigned int)ret);

	
	exit:
	
	iruserExit();

	printf("Press START to exit.\n");

	// Main loop
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();

		u32 kDown = hidKeysDown();

		if (kDown & KEY_START)
			break; // break in order to return to hbmenu
	}

	gfxExit();
	return 0;
}

