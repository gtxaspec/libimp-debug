/*
 * libimp-nodbg.so - Suppress IMP debug shm thread in secondary processes
 *
 * LD_PRELOAD this on any raptor daemon that links libimp but should NOT
 * run the debug dispatch thread (e.g. RAD). The primary process (RVD)
 * runs without this preload and owns the shm_thread.
 *
 * We must stub all entry points that create the shm_thread:
 *   DsystemInit -> func_init -> shm_init + shm_register_cb
 *   IMP_AI_IMPDBG_Init -> func_init -> shm_init + shm_register_cb
 *
 * Usage: LD_PRELOAD=/usr/lib/libimp-nodbg.so rad -c /etc/raptor.conf
 */

int func_init(void) { return 0; }
void DsystemInit(void) { }
void DsystemExit(void) { }
