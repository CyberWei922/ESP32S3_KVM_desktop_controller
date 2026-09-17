#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface KVMDDCDisplay : NSObject

@property(nonatomic, copy, readonly) NSString *uuid;
@property(nonatomic, copy, readonly) NSString *name;
@property(nonatomic, copy, readonly) NSString *manufacturer;
@property(nonatomic, assign, readonly) uint32_t vendor;
@property(nonatomic, assign, readonly) uint32_t model;
@property(nonatomic, assign, readonly) uint32_t serial;
@property(nonatomic, assign, readonly, getter=isDDCAvailable) BOOL ddcAvailable;

- (instancetype)initWithUUID:(NSString *)uuid
                        name:(NSString *)name
                manufacturer:(NSString *)manufacturer
                      vendor:(uint32_t)vendor
                       model:(uint32_t)model
                      serial:(uint32_t)serial
                ddcAvailable:(BOOL)ddcAvailable;

@end

@interface KVMDDCBridge : NSObject

+ (NSArray<KVMDDCDisplay *> *)availableDisplays;
+ (BOOL)setInput:(uint16_t)value
     displayUUID:(NSString *)displayUUID
    errorMessage:(NSString * _Nullable * _Nullable)errorMessage;
+ (BOOL)readInputForDisplayUUID:(NSString *)displayUUID
                           value:(uint16_t *)value
                    errorMessage:(NSString * _Nullable * _Nullable)errorMessage;

@end

NS_ASSUME_NONNULL_END
