/* SPDX-License-Identifier: MIT */
#include <librustyui/theme.h>
#include <glib.h>
#include <string.h>
static const char *names[RRUI_THEME_COUNT]={
   "workspace.background","panel.background","panel.header","panel.border",
   "panel.focus","text.normal","text.accent","input.background","selection.background",
   "status.success","status.warning","status.tx","panel.header.active"
};
void rrui_theme_default(rrui_theme_t *t) {
   if(t)*t=(rrui_theme_t){{0x111920,0x15252d,0x26434f,0x527b89,
      0x91d5da,0xc5d6db,0x91d5da,0x20343e,0x385666,0x93ce9b,0xe4c477,0xf07878,0x91d5da}};
}
const char *rrui_theme_name(rrui_theme_role_t r){return (unsigned)r<RRUI_THEME_COUNT?names[r]:NULL;}
bool rrui_theme_load(rrui_theme_t *t,const char *path) {
   if(!t||!path)return false;
   GKeyFile *file=g_key_file_new();
   bool ok=g_key_file_load_from_file(file,path,G_KEY_FILE_NONE,NULL)&&g_key_file_has_group(file,"theme");
   rrui_theme_t next;rrui_theme_default(&next);
   for(unsigned i=0;ok&&i<RRUI_THEME_COUNT;i++){
      if(!g_key_file_has_key(file,"theme",names[i],NULL))continue;
      char *s=g_key_file_get_string(file,"theme",names[i],NULL);
      if(!s||strlen(s)!=7||s[0]!='#'||strspn(s+1,"0123456789abcdefABCDEF")!=6)ok=false;
      else next.rgb[i]=(uint32_t)g_ascii_strtoull(s+1,NULL,16);
      g_free(s);
   }
   g_key_file_unref(file);if(ok)*t=next;return ok;
}
