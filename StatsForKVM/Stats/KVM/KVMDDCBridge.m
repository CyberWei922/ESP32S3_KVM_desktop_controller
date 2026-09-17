#import "KVMDDCBridge.h"

#import "i2c.h"
#import "ioregistry.h"

@implementation KVMDDCDisplay

- (instancetype)initWithUUID:(NSString *)uuid
                        name:(NSString *)name
                manufacturer:(NSString *)manufacturer
                      vendor:(uint32_t)vendor
                       model:(uint32_t)model
                      serial:(uint32_t)serial
                ddcAvailable:(BOOL)ddcAvailable {
    self = [super init];
    if (self) {
        _uuid = [uuid copy];
        _name = [name copy];
        _manufacturer = [manufacturer copy];
        _vendor = vendor;
        _model = model;
        _serial = serial;
        _ddcAvailable = ddcAvailable;
    }
    return self;
}

@end

static void KVMReleaseDisplayInfos(DisplayInfos *displays, CGDisplayCount count) {
    releaseDisplayInfos(displays, count);
}

static DisplayInfos *KVMFindDisplay(DisplayInfos *displays, CGDisplayCount count, NSString *uuid) {
    for (CGDisplayCount index = 0; index < count; index++) {
        if ([displays[index].uuid isEqualToString:uuid]) {
            return &displays[index];
        }
    }
    return NULL;
}

static NSString *KVMErrorForIOReturn(IOReturn result) {
    const char *description = mach_error_string(result);
    if (description == NULL) {
        return [NSString stringWithFormat:@"ddc_error_%d", result];
    }
    return [NSString stringWithFormat:@"DDC communication failure: %s", description];
}

@implementation KVMDDCBridge

+ (NSArray<KVMDDCDisplay *> *)availableDisplays {
    DisplayInfos displays[MAX_DISPLAYS] = {};
    CGDisplayCount count = getOnlineDisplayInfos(displays);
    NSMutableArray<KVMDDCDisplay *> *result = [NSMutableArray arrayWithCapacity:count];

    for (CGDisplayCount index = 0; index < count; index++) {
        DDCTransport transport = getDisplayDDCTransport(&displays[index]);
        BOOL available = transport.service != NULL;
        if (transport.service != NULL) {
            CFRelease(transport.service);
        }
        KVMDDCDisplay *display = [[KVMDDCDisplay alloc]
            initWithUUID:displays[index].uuid ?: @""
            name:displays[index].productName ?: @"Unknown Display"
            manufacturer:displays[index].manufacturer ?: @""
            vendor:displays[index].vendor
            model:displays[index].model
            serial:displays[index].serial
            ddcAvailable:available];
        [result addObject:display];
    }

    KVMReleaseDisplayInfos(displays, count);
    return result;
}

+ (BOOL)setInput:(uint16_t)value
     displayUUID:(NSString *)displayUUID
    errorMessage:(NSString * _Nullable * _Nullable)errorMessage {
    if (value == 0) {
        if (errorMessage != NULL) *errorMessage = @"invalid_input_value";
        return NO;
    }

    DisplayInfos displays[MAX_DISPLAYS] = {};
    CGDisplayCount count = getOnlineDisplayInfos(displays);
    DisplayInfos *display = KVMFindDisplay(displays, count, displayUUID);
    if (display == NULL) {
        if (errorMessage != NULL) *errorMessage = @"display_not_found";
        KVMReleaseDisplayInfos(displays, count);
        return NO;
    }

    DDCTransport transport = getDisplayDDCTransport(display);
    if (transport.service == NULL) {
        if (errorMessage != NULL) *errorMessage = @"ddc_transport_unavailable";
        KVMReleaseDisplayInfos(displays, count);
        return NO;
    }

    DDCPacket packet = createDDCPacket(INPUT);
    prepareDDCWrite(&packet, value);
    IOReturn result = performDDCWriteAtChipAddress(transport.service, transport.chipAddress, &packet);
    CFRelease(transport.service);
    KVMReleaseDisplayInfos(displays, count);

    if (result != kIOReturnSuccess) {
        if (errorMessage != NULL) *errorMessage = KVMErrorForIOReturn(result);
        return NO;
    }
    return YES;
}

+ (BOOL)readInputForDisplayUUID:(NSString *)displayUUID
                           value:(uint16_t *)value
                    errorMessage:(NSString * _Nullable * _Nullable)errorMessage {
    DisplayInfos displays[MAX_DISPLAYS] = {};
    CGDisplayCount count = getOnlineDisplayInfos(displays);
    DisplayInfos *display = KVMFindDisplay(displays, count, displayUUID);
    if (display == NULL) {
        if (errorMessage != NULL) *errorMessage = @"display_not_found";
        KVMReleaseDisplayInfos(displays, count);
        return NO;
    }

    DDCTransport transport = getDisplayDDCTransport(display);
    if (transport.service == NULL) {
        if (errorMessage != NULL) *errorMessage = @"ddc_transport_unavailable";
        KVMReleaseDisplayInfos(displays, count);
        return NO;
    }

    DDCPacket packet = createDDCPacket(INPUT);
    prepareDDCRead(packet.data);
    IOReturn writeResult = performDDCWriteAtChipAddress(transport.service, transport.chipAddress, &packet);
    if (writeResult != kIOReturnSuccess) {
        if (errorMessage != NULL) *errorMessage = KVMErrorForIOReturn(writeResult);
        CFRelease(transport.service);
        KVMReleaseDisplayInfos(displays, count);
        return NO;
    }

    DDCPacket response = {};
    response.inputAddr = packet.inputAddr;
    IOReturn readResult = performDDCReadAtChipAddress(transport.service, transport.chipAddress, &response);
    CFRelease(transport.service);
    KVMReleaseDisplayInfos(displays, count);
    if (readResult != kIOReturnSuccess) {
        if (errorMessage != NULL) *errorMessage = KVMErrorForIOReturn(readResult);
        return NO;
    }

    DDCValue decoded = convertI2CtoDDC((char *)response.data);
    // VCP input source 0 is invalid. An all-zero response is a common empty
    // DDC read and must not be presented as a successful read-back.
    if (decoded.curValue <= 0 || decoded.curValue > UINT16_MAX) {
        if (errorMessage != NULL) *errorMessage = @"invalid_ddc_response";
        return NO;
    }
    if (value != NULL) *value = (uint16_t)decoded.curValue;
    return YES;
}

@end
