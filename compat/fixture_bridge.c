/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_sfs_fixture_host_v1.h"
#include <errno.h>
static _Thread_local const IfsSfsFixtureHostV1 *active;
int ifs_bind_fixture(const IfsSfsFixtureHostV1 *h) {
 if(!h || active || h->abi_version != IFS_SFS_FIXTURE_HOST_V1 ||
    h->structure_bytes < sizeof(*h) || !h->find_spec || !h->fragment_profile ||
    !h->profile_file_chunks || !h->profile_file_bytes || !h->profile_payload_bytes) {
    errno=EINVAL; return -1;
 }
 active=h; return 0;
}
void ifs_unbind_fixture(void) { active=NULL; }
const LdtmFilesystemSpec *ldtm_find_spec(const char *k) { return active ? active->find_spec(k) : NULL; }
LdtmFragmentProfile ldtm_fragment_profile(const LdtmFilesystemSpec *s) {
 LdtmFragmentProfile zero={0};
 return active ? active->fragment_profile(s) : zero;
}
uint32_t ldtm_profile_file_chunks(const LdtmFragmentProfile *p, size_t i) {
 return active ? active->profile_file_chunks(p,i) : 0u;
}
uint64_t ldtm_profile_file_bytes(const LdtmFragmentProfile *p, size_t i) {
 return active ? active->profile_file_bytes(p,i) : 0u;
}
uint64_t ldtm_profile_payload_bytes(const LdtmFragmentProfile *p) {
 return active ? active->profile_payload_bytes(p) : 0u;
}
