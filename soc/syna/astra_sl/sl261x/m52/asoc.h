#ifndef __ASOC_H__
#define __ASOC_H__

#ifdef __cplusplus
extern "C" {
#endif

int asoc_setup(void);
void release_asoc(uint32_t initvtor);

#ifdef __cplusplus
}
#endif

#endif /* __ASOC_H__ */