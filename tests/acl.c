#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/acl.h>
static bool group(const char *name, void *context) {
   return !strcmp(name, context);
}
static acl_result_t match(bool allow, const char *selector, const char *ip, const char *user) {
   acl_rule_t rule;
   acl_subject_t subject;
   assert(acl_rule_parse(&rule, allow, selector));
   assert(acl_subject_init(&subject, ip, user, group, "admin"));

   return acl_check(&rule, 1, &subject);
}
int main(void) {
   acl_subject_t subject;
   assert(acl_subject_init(&subject, "10.200.4.5", NULL, NULL, NULL));
   assert(acl_check(NULL, 0, &subject) == ACL_DENY);
   assert(match(true, "all", "203.0.113.1", NULL) == ACL_ALLOW);
   assert(match(false, "all", "::1", NULL) == ACL_DENY);
   assert(match(true, "guest@all", "::1", NULL) == ACL_DEFER);
   assert(match(true, "10.0.0.0/8", "10.255.255.255", "guest") == ACL_ALLOW);
   assert(match(true, "10.0.0.0/8", "11.0.0.0", "guest") == ACL_DENY);
   assert(match(true, "0.0.0.0/0", "255.255.255.255", "guest") == ACL_ALLOW);
   assert(match(true, "192.0.2.3/32", "192.0.2.2", "guest") == ACL_DENY);
   assert(match(true, "2001:db8::/65", "2001:db8:0:0:7fff::", "guest") == ACL_ALLOW);
   assert(match(true, "2001:db8::/65", "2001:db8:0:0:8000::", "guest") == ACL_DENY);
   assert(match(true, "::1/128", "::2", "guest") == ACL_DENY);
   assert(match(true, "10.0.0.0/8", "::ffff:10.2.3.4", "guest") == ACL_ALLOW);
   assert(match(true, "::ffff:10.0.0.0/104", "10.2.3.4", "guest") == ACL_ALLOW);
   assert(match(true, "@admin", "::1", "user") == ACL_ALLOW);
   assert(match(true, "@operator", "::1", "user") == ACL_DENY);
   assert(match(true, "@admin@10.0.0.0/8", "10.1.2.3", "user") == ACL_ALLOW);
   assert(match(true, "guest", "::1", "guest") == ACL_ALLOW);
   assert(match(true, "guest", "::1", "GUEST") == ACL_DENY);
   assert(match(true, "guest", "::1", "") == ACL_DENY);
   assert(match(true, "10.0.0.0/8", NULL, "guest") == ACL_DEFER);
   assert(match(true, "guest@10.0.0.0/8", NULL, "operator") == ACL_DENY);
   assert(match(true, "@admin@10.0.0.0/8", NULL, "guest") == ACL_DEFER);
   acl_rule_t rules[3];
   assert(acl_rule_parse(rules, false, "guest@10.200.0.0/16"));
   assert(acl_rule_parse(rules + 1, true, "10.0.0.0/8"));
   assert(acl_rule_parse(rules + 2, false, "all"));
   assert(acl_check(rules, 3, &subject) == ACL_DEFER);
   subject.user = "guest";
   assert(acl_check(rules, 3, &subject) == ACL_DENY);
   subject.user = "operator";
   assert(acl_check(rules, 3, &subject) == ACL_ALLOW);
   subject.user = "GUEST";
   subject.ignore_case = true;
   assert(acl_check(rules, 3, &subject) == ACL_DENY);
   assert(acl_check(rules + 1, 2, &subject) == ACL_ALLOW);
   const char *invalid[] = {
      "", "@", "user@", "user@@all", "10.0.0.0/33", "::/129", "::/-1", "10.0.0.0/8junk", "999.1.2.3", "10.2.3", "all/0", "localhost/8", "user name",
      "allow=all"
   };

   for (size_t i = 0 ; i < sizeof(invalid) / sizeof(invalid[0]) ; i++) {
      assert(!acl_rule_parse(rules, true, invalid[i]));
   }

   assert(!acl_subject_init(&subject, "hostname.example", NULL, NULL, NULL));
   puts("PASS: ordered ACLs, implicit deny, all/user/group rules, authentication deferral, CIDR boundaries, IPv6 and mapped IPv4");
}
