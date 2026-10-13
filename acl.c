#include <librustyaxe/acl.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#ifdef ESP_PLATFORM
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#endif

static bool mapped(const uint8_t *address) {
   static const uint8_t prefix[12] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff
   };

   return !memcmp(address, prefix, sizeof(prefix));
}
static bool address_parse(const char *ip, uint8_t *address, unsigned *family) {
   memset(address, 0, 16);

   if (inet_pton(AF_INET, ip, address) == 1) {
      *family = 4;
   } else if (inet_pton(AF_INET6, ip, address) == 1) {
      *family = 6;
   } else {
      return false;
   }

   return true;
}
static bool network_parse(acl_rule_t *rule, const char *text) {
   if (!strcmp(text, "all") || !strcmp(text, "*")) {
      return true;
   }
   char ip[64];

   if (strlen(text) >= sizeof(ip)) {
      return false;
   }
   strcpy(ip, text);
   char *slash = strchr(ip, '/');
   unsigned prefix = 0;

   if (slash) {
      *slash++ = 0;

      if (!*slash) {
         return false;
      }

      for (const char *p = slash ; *p ; p++) {
         if (!isdigit((unsigned char)*p) || prefix > 128) {
            return false;
         }
         prefix = prefix * 10 + (*p - '0');
      }
   }

   if (!address_parse(ip, rule->address, &rule->family)) {
      return false;
   }
   unsigned bits = rule->family == 4 ? 32 : 128;
   rule->prefix = slash ? prefix : bits;

   if (rule->prefix > bits) {
      return false;
   }

   if (rule->family == 6 && mapped(rule->address) && rule->prefix >= 96) {
      memmove(rule->address, rule->address + 12, 4);
      rule->family = 4;
      rule->prefix -= 96;
   }

   return true;
}
bool acl_rule_parse(acl_rule_t *rule, bool allow, const char *selector) {
   if (!rule || !selector || !*selector || strlen(selector) >= 256) {
      return false;
   }
   acl_rule_t parsed = {
      .allow = allow
   };
   char copy[256];
   strcpy(copy, selector);

   for (const char *p = copy ; *p ; p++) {
      if (isspace((unsigned char)*p) || *p == '=') {
         return false;
      }
   }

   char *at = strrchr(copy, '@');
   const char *principal = NULL, *network = NULL;

   if (at && at != copy) {
      *at++ = 0;
      network = at;
      principal = copy;
   } else if (copy[0] == '@') {
      principal = copy;
   } else if (!strcmp(copy, "all") || !strcmp(copy, "*") || strchr(copy, '/') || strchr(copy, ':') || address_parse(copy, parsed.address, &parsed.family)) {
      parsed.family = 0;
      network = copy;
   } else {
      /* A dotted all-numeric token must not turn an invalid IP into a user. */
      bool numeric = true;

      for (const char *p = copy ; *p ; p++) {
         numeric &= isdigit((unsigned char)*p) || *p == '.';
      }

      if (numeric && strchr(copy, '.')) {
         return false;
      }
      principal = copy;
   }

   if (principal) {
      parsed.kind = *principal == '@' ? ACL_GROUP : ACL_USER;

      if (parsed.kind == ACL_GROUP) {
         principal++;
      }

      if (!*principal || strlen(principal) > ACL_PRINCIPAL_MAX || strchr(principal, '@')) {
         return false;
      }
      strcpy(parsed.principal, principal);
   }

   if (network && !network_parse(&parsed, network)) {
      return false;
   }
   *rule = parsed;

   return true;
}
bool acl_subject_init(acl_subject_t *subject, const char *ip, const char *user, acl_group_fn in_group, void *context) {
   if (!subject) {
      return false;
   }
   *subject = (acl_subject_t) {
      .user = user, .in_group = in_group, .context = context
   };

   if (!ip) {
      return true;
   }

   if (!address_parse(ip, subject->address, &subject->family)) {
      return false;
   }

   if (subject->family == 6 && mapped(subject->address)) {
      memmove(subject->address, subject->address + 12, 4);
      subject->family = 4;
   }

   return true;
}
acl_result_t acl_rule_match(const acl_rule_t *rule, const acl_subject_t *subject) {
   if (!rule || !subject) {
      return ACL_NO_MATCH;
   }
   bool pending = rule->family && !subject->family;

   if (rule->family && subject->family) {
      if ((rule->family != 4 && rule->family != 6) || rule->prefix > (rule->family == 4 ? 32 : 128) || rule->family != subject->family) {
         return ACL_NO_MATCH;
      }
      unsigned bytes = rule->prefix / 8, bits = rule->prefix % 8;

      if (memcmp(rule->address, subject->address, bytes)) {
         return ACL_NO_MATCH;
      }

      if (bits && ((rule->address[bytes] ^ subject->address[bytes]) & (0xffU << (8 - bits)))) {
         return ACL_NO_MATCH;
      }
   }

   if (rule->kind != ACL_ANY) {
      if (!subject->user) {
         return ACL_DEFER;
      }

      if (rule->kind == ACL_USER) {
         if (subject->ignore_case ? strcasecmp(rule->principal, subject->user) : strcmp(rule->principal, subject->user)) {
            return ACL_NO_MATCH;
         }
      } else if (!subject->in_group || !subject->in_group(rule->principal, subject->context)) {
         return ACL_NO_MATCH;
      }
   }

   return pending ? ACL_DEFER : rule->allow ? ACL_ALLOW : ACL_DENY;
}
acl_result_t acl_check(const acl_rule_t *rules, size_t count, const acl_subject_t *subject) {
   if (!rules || !subject) {
      return ACL_DENY;
   }

   for (size_t i = 0 ; i < count ; i++) {
      acl_result_t result = acl_rule_match(&rules[i], subject);

      if (result != ACL_NO_MATCH) {
         return result;
      }
   }

   return ACL_DENY;
}
