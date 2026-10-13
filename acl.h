/* Ordered access rules, independent of configuration, sockets and account storage. */
#ifndef LIBRUSTYAXE_ACL_H
#define LIBRUSTYAXE_ACL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ACL_PRINCIPAL_MAX 127

typedef enum {
   ACL_NO_MATCH, ACL_ALLOW, ACL_DENY, ACL_DEFER
} acl_result_t;
typedef enum {
   ACL_ANY, ACL_USER, ACL_GROUP
} acl_principal_t;
typedef bool (*acl_group_fn)(const char *group, void *context);
typedef struct {
   uint8_t address[16];
   unsigned family, prefix;
   acl_principal_t kind;
   char principal[ACL_PRINCIPAL_MAX + 1];
   bool allow;
} acl_rule_t;
typedef struct {
   uint8_t address[16];
   unsigned family;
   const char *user;            /* NULL: identity unknown; "": known anonymous. */
   acl_group_fn in_group;
   void *context;
   bool ignore_case;            /* User comparison only; callback owns groups. */
} acl_subject_t;
/* Selectors: all, IP[/prefix], user, @group, user@IP[/prefix], user@all. Also accepts @group@IP[/prefix]. No hostname resolution or shell patterns. Invalid
 * input returns false and must not be silently omitted by the caller. */
bool acl_rule_parse(acl_rule_t *rule, bool allow, const char *selector);
/* IP is a numeric IPv4/IPv6 address, or NULL while the source address is unknown. IPv4-mapped IPv6 addresses are normalized to IPv4. No allocations. */
bool acl_subject_init(acl_subject_t *subject, const char *ip, const char *user, acl_group_fn in_group, void *context);
/* A potentially matching rule returns DEFER if its address or identity is unknown. It must be checked again after authentication; DEFER is never authorization.
 */
acl_result_t acl_rule_match(const acl_rule_t *rule, const acl_subject_t *subject);
/* First match wins. No match, NULL rules or zero rules deny by default. */
acl_result_t acl_check(const acl_rule_t *rules, size_t count, const acl_subject_t *subject);
#endif
