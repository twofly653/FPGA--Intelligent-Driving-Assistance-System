#include "jpeg_photo.h"

static unsigned be16(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }

jpeg_photo_error_t jpeg_photo_validate(const uint8_t *p,size_t size,
                                     unsigned *width,unsigned *height)
{
    size_t pos=2, entropy=0;
    unsigned saw_sof=0,saw_scan=0,w=0,h=0;
    if(width) *width=0;
    if(height) *height=0;
    if(!p || size<4) return JPEG_PHOTO_TRUNCATED;
    if(p[0]!=0xff || p[1]!=0xd8) return JPEG_PHOTO_MARKER;
    while(pos<size) {
        unsigned marker,n;
        const uint8_t *data;
        if(p[pos++]!=0xff) return JPEG_PHOTO_MARKER;
        while(pos<size && p[pos]==0xff) ++pos;
        if(pos>=size) return JPEG_PHOTO_TRUNCATED;
        marker=p[pos++];
        if(marker==0xd9) {
            if(pos!=size || !saw_sof || !saw_scan || !entropy)
                return JPEG_PHOTO_MARKER;
            if(width) *width=w;
            if(height) *height=h;
            return (w==2592 && h==1944) ? JPEG_PHOTO_OK : JPEG_PHOTO_DIMENSIONS;
        }
        if(marker==0 || marker==0xd8 || marker==1 || (marker>=0xd0 && marker<=0xd7))
            return JPEG_PHOTO_MARKER;
        if(pos+2>size) return JPEG_PHOTO_TRUNCATED;
        n=be16(p+pos);
        if(n<2 || n>size-pos) return JPEG_PHOTO_TRUNCATED;
        data=p+pos+2;
        if(marker==0xc0 || marker==0xc2) {
            if(saw_sof || n<8 || data[0]!=8 || data[5]<1 || data[5]>4 ||
               n!=8+3u*data[5]) return JPEG_PHOTO_UNSUPPORTED;
            h=be16(data+1); w=be16(data+3); saw_sof=1;
        } else if(marker>=0xc0 && marker<=0xcf && marker!=0xc4 && marker!=0xc8 && marker!=0xcc) {
            return JPEG_PHOTO_UNSUPPORTED;
        }
        if(marker==0xda) {
            if(!saw_sof || n<6 || data[0]<1 || data[0]>4 || n!=6+2u*data[0])
                return JPEG_PHOTO_MARKER;
            saw_scan=1;
        }
        pos+=n;
        if(marker==0xda) {
            /* Stuffed FF00 and restart markers remain entropy data. A real
             * marker ends the scan; progressive files may then start another. */
            while(pos<size) {
                size_t start=pos;
                unsigned next;
                if(p[pos]!=0xff) { ++pos; ++entropy; continue; }
                while(pos<size && p[pos]==0xff) ++pos;
                if(pos>=size) return JPEG_PHOTO_TRUNCATED;
                next=p[pos];
                if(next==0) { ++pos; ++entropy; }
                else if(next>=0xd0 && next<=0xd7) ++pos;
                else { pos=start; break; }
            }
        }
    }
    return JPEG_PHOTO_TRUNCATED;
}

uint32_t jpeg_photo_crc32(const uint8_t *bytes,size_t size)
{
    static uint32_t table[256];
    static int initialized;
    uint32_t crc=0xffffffffu;
    size_t i;
    if(!initialized) {
        unsigned j;
        for(i=0;i<256;++i) {
            uint32_t v=(uint32_t)i;
            for(j=0;j<8;++j) v=(v>>1)^((v&1u)?0xedb88320u:0);
            table[i]=v;
        }
        initialized=1;
    }
    for(i=0;i<size;++i) crc=table[(crc^bytes[i])&255u]^(crc>>8);
    return crc^0xffffffffu;
}
