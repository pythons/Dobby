#include "dobby_internal.h"

#include "MachUtility.h"

#include "PlatformUtil/ProcessRuntimeUtility.h"

#include <unistd.h>
#include <stdlib.h>

#include <iostream>

#include "async_logger.h"

extern char *mach_msg_to_str(mach_msg_header_t *msg);

#if 0
typeof(mach_msg) *orig_mach_msg = NULL;

mach_msg_return_t fake_mach_msg(mach_msg_header_t *msg, mach_msg_option_t option, mach_msg_size_t send_size,
                                mach_msg_size_t rcv_size, mach_port_name_t rcv_name, mach_msg_timeout_t timeout,
                                mach_port_name_t notify) {
  char buffer[256] = {0};
  char *mach_msg_name = mach_msg_to_str(msg);
  if(mach_msg_name) {
    sprintf(buffer, "[%d][mach_msg] %s\n",i++, mach_msg_name);
    async_logger_print(buffer);
  }
#if 0
  {
    write(STDOUT_FILENO, buffer, strlen(buffer) + 1);
  }
#endif
  return orig_mach_msg(msg, option, send_size, rcv_size, rcv_name, timeout, notify);
}

void mach_system_call_monitor() {
  void *mach_msg_ptr = (void *)DobbySymbolResolver(NULL, "mach_msg");
  log_set_level(1);
  DobbyHook(mach_msg_ptr, (void *)fake_mach_msg, (void **)&orig_mach_msg);
}
#endif


static addr_t getCallFirstArg(RegisterContext *reg_ctx) {
  addr_t result;
#if defined(_M_X64) || defined(__x86_64__)
#if defined(_WIN32)
  result = reg_ctx->general.regs.rcx;
#else
  result = reg_ctx->general.regs.rdi;
#endif
#elif defined(__arm64__) || defined(__aarch64__)
  result = reg_ctx->general.regs.x0;
#elif defined(__arm__)
  result = reg_ctx->general.regs.r0;
#else
#error "Not Support Architecture."
#endif
  return result;
}

static addr_t getRealLr(RegisterContext *ctx) {
  addr_t closure_trampoline_reserved_stack = ctx->sp - sizeof(addr_t);
  return *(addr_t *)closure_trampoline_reserved_stack;
}

addr_t get_caller_from_main_binary(RegisterContext *ctx) {
  static addr_t text_section_start = 0, text_section_end = 0;
  static addr_t slide = 0;
  if(text_section_start == 0 || text_section_end == 0) {
    auto   main        = ProcessRuntimeUtility::GetProcessModuleMap()[0];
    addr_t main_header = (addr_t)main.load_address;
    
    auto text_segment = mach_kit::macho_get_segment_by_name_64((struct mach_header_64 *)main_header, "__TEXT");
    slide  = main_header - text_segment->vmaddr;
    
    auto text_section  = mach_kit::macho_get_section_by_name_64((struct mach_header_64 *)main_header, "__TEXT", "__text");
    text_section_start     = main_header + (addr_t)text_section->offset;
    text_section_end = text_section_start + text_section->size;
  }
  
  addr_t lr = getRealLr(ctx);
  if(lr > text_section_start && lr < text_section_end)
    return lr - slide;
  
#define MAX_STACK_ITERATE_LEVEL 4
  addr_t fp = ctx->fp;
  for (int i = 0; i < MAX_STACK_ITERATE_LEVEL; i++) {
    addr_t lr = *(addr_t *)(fp + sizeof(addr_t));
    if(lr > text_section_start && lr < text_section_end)
      return lr - slide;
    fp = *(addr_t *)fp;
  }
  return 0;
}

static void common_handler(RegisterContext *reg_ctx, const HookEntryInfo *info) {
  addr_t caller = get_caller_from_main_binary(reg_ctx);
  if (caller == 0)
    return;
  
  char buffer[256] = {0};
  mach_msg_header_t *msg = (typeof(msg))getCallFirstArg(reg_ctx);
  char *mach_msg_name = mach_msg_to_str(msg);
  if(mach_msg_name) {
    sprintf(buffer, "[mach msg %p] %s\n", caller, mach_msg_name);
  } else {
    buffer[0] = 0;
  }
  if(buffer[0])
    async_logger_print(buffer);
}

void mach_system_call_monitor() {
  void *mach_msg_ptr = (void *)DobbySymbolResolver(NULL, "mach_msg");
  log_set_level(1);
  DobbyInstrument(mach_msg_ptr, common_handler);
}
