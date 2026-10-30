/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RECOVERY_DASHBOARD_H
#define KUI_RECOVERY_DASHBOARD_H
#include "kui/shell.h"
#include <stdio.h>
#include <string.h>

/* Presentation cache only. Neither a matching title nor this snapshot admits
 * a job or authorizes repair; every operation still uses normal admission. */
static inline void kui_recovery_dashboard_clear(struct kui_salvage_dashboard *view) {
    *view=(struct kui_salvage_dashboard){0};
}
static inline bool kui_recovery_dashboard_job(const char *job) {
    if(!job || strlen(job)!=21 || strncmp(job,"/KUI/salvage/job-",17) || !strcmp(job+17,"0000"))return false;
    for(unsigned i=17;i<21;++i)if(job[i]<'0' || job[i]>'9')return false;
    return true;
}
static inline bool kui_recovery_dashboard_admit(struct kui_salvage_dashboard *view,
        const char *job,const char *title) {
    if(!kui_recovery_dashboard_job(job))return false;
    /* Callers may refresh from this same snapshot; take alias-safe copies
     * before clearing a changed job or replacing either text buffer. */
    char saved_job[22],saved_title[96]={0};
    memcpy(saved_job,job,sizeof(saved_job));
    bool title_known=title && title[0];
    if(title_known)snprintf(saved_title,sizeof(saved_title),"%.95s",title);
    if(!view->known || strcmp(view->job,job)) {
        kui_recovery_dashboard_clear(view);
        memcpy(view->job,saved_job,sizeof(saved_job));
    }
    view->known=true;
    if(title_known)memcpy(view->title,saved_title,sizeof(saved_title));
    return true;
}
static inline bool kui_recovery_dashboard_counts(struct kui_salvage_dashboard *view,
        const char *job,uint32_t targets,uint32_t recovered,uint32_t remaining) {
    if(!view->known || !job || strcmp(view->job,job) || recovered>targets || remaining!=targets-recovered)return false;
    view->counts_known=true;view->targets=targets;view->recovered=recovered;view->remaining=remaining;
    return true;
}
static inline bool kui_recovery_dashboard_guard(struct kui_salvage_dashboard *view,
        const char *job,bool blocked) {
    if(!view->known || !job || strcmp(view->job,job))return false;
    view->blocked=blocked;return true;
}
#endif
