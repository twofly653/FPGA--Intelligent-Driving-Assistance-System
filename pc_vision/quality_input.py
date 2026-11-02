"""Bit-exact RGB565 sampling models and conservative preview enhancement."""
from track_video import cv2,np

def rgb565(image):
    b,g,r=(image[:,:,k].astype(np.uint16) for k in range(3))
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3)

def decode565(a):
    r=(a>>11)&31;g=(a>>5)&63;b=a&31
    return np.stack(((b<<3)|(b>>2),(g<<2)|(g>>4),(r<<3)|(r>>2)),axis=-1).astype(np.uint8)

def average565(a):
    """Rounded mean in native 5/6/5 components; no cross-channel carry."""
    if a.shape[0]%2 or a.shape[1]%2:raise ValueError('even dimensions required')
    out=np.zeros((a.shape[0]//2,a.shape[1]//2),np.uint16)
    for shift,mask in ((11,31),(5,63),(0,31)):
        c=(a>>shift)&mask
        total=c[::2,::2]+c[::2,1::2]+c[1::2,::2]+c[1::2,1::2]
        out|=((total+2)>>2)<<shift
    return out

def camera_input(original,profile):
    h,w=original.shape[:2];scale=min(640/w,480/h)
    rw,rh=round(w*scale),round(h*scale)
    full=np.zeros((480,640,3),np.uint8)
    full[(480-rh)//2:(480-rh)//2+rh,(640-rw)//2:(640-rw)//2+rw]=cv2.resize(original,(rw,rh),interpolation=cv2.INTER_AREA)
    raw=rgb565(full)
    if profile=='legacy320':raw=raw[::2,::2]
    elif profile=='antialias320':raw=average565(raw)
    elif profile!='native640':raise ValueError(profile)
    return raw.astype('<u2').tobytes(),decode565(raw)

def display_detail(image):
    # Luminance only, +/-6 levels, noise deadband; never changes detector input.
    ycc=cv2.cvtColor(image,cv2.COLOR_BGR2YCrCb);y=ycc[:,:,0]
    diff=y.astype(np.int16)-cv2.GaussianBlur(y,(3,3),.8).astype(np.int16)
    detail=np.where(np.abs(diff)>3,np.clip(diff*.3,-6,6),0)
    ycc[:,:,0]=np.clip(y.astype(np.float32)+detail,0,255).astype(np.uint8)
    return cv2.cvtColor(ycc,cv2.COLOR_YCrCb2BGR)
