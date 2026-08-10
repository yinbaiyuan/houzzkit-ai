#ifndef _TIME_SYNC_H
#define _TIME_SYNC_H

struct cJSON;

bool SyncServerTimeFromJson(const cJSON* server_time, const char* source);

#endif // _TIME_SYNC_H
