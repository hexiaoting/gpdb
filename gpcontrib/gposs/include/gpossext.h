#ifndef _GPOSSEXT_H_
#define _GPOSSEXT_H_

// TODO include GpId from proper place
typedef struct GpId {
    int32_t numsegments; /* count of distinct segindexes */
    int32_t dbid;        /* the dbid of this database */
    int32_t segindex;    /* content indicator: -1 for entry database,
                       * 0, ..., n-1 for segment database *
                       * a primary and its mirror have the same segIndex */
} GpId;
extern GpId GpIdentity;

#endif
