#ifndef __CVI_EPHY_H__
#define __CVI_EPHY_H__

/*
 * CVITEK internal EPHY (CV181x) top register setup: APB interface select,
 * release from shutdown, PHY ID programming. Implemented in
 * libraries/eth/cvi_ephy.c; consumed by the eth adapter.
 */

void cvi_ephy_id_init(void);

#endif /* __CVI_EPHY_H__ */
