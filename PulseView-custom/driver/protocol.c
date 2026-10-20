/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <config.h>
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>

SR_PRIV int rp2350_command(struct dev_context *d, uint8_t type, const uint8_t *p, size_t n)
{
    uint8_t frame[LAR2_MAX_FRAME];
    size_t len=lar2_encode(frame,type,d->tx_seq++,p,n), pos=0;
    gint64 deadline=g_get_monotonic_time()+1000000;
    while (pos<len && g_get_monotonic_time()<deadline) {
        ssize_t wr=write(d->fd,frame+pos,len-pos);
        if (wr>0) pos+=wr;
        else if (wr<0 && errno!=EAGAIN && errno!=EINTR) return SR_ERR_IO;
        else { struct pollfd f={d->fd,POLLOUT,0}; poll(&f,1,10); }
    }
    return pos==len ? SR_OK : SR_ERR_TIMEOUT;
}

/* Read one byte at a time during RPC so capture frames after ARM ACK remain
 * queued for the asynchronous session callback. */
SR_PRIV int rp2350_reply(struct dev_context *d, uint8_t type, int timeout_ms)
{
    gint64 end=g_get_monotonic_time()+(gint64)timeout_ms*1000;
    while (g_get_monotonic_time()<end) {
        uint8_t b;
        ssize_t n=read(d->fd,&b,1);
        if (n==1) {
            int r=lar2_feed(&d->parser,b);
            if (r<0) { sr_err("Malformed LAR2 reply/CRC."); return SR_ERR_DATA; }
            if (!r) continue;
            if (lar2_u16(d->parser.buf+6)!=d->rx_seq++) return SR_ERR_DATA;
            if (d->parser.buf[5]!=type) {
                sr_err("Unexpected reply 0x%02x (expected 0x%02x).",d->parser.buf[5],type);
                return SR_ERR_DATA;
            }
            return SR_OK;
        }
        if (n<0 && errno!=EAGAIN && errno!=EINTR) return SR_ERR_IO;
        { struct pollfd f={d->fd,POLLIN,0};
          if (poll(&f,1,10)>0 && (f.revents&(POLLERR|POLLHUP|POLLNVAL))) return SR_ERR_IO; }
    }
    sr_err("LAR2 handshake timeout; check firmware and CDC port.");
    return SR_ERR_TIMEOUT;
}

SR_PRIV int rp2350_connect(struct dev_context *d)
{
    struct termios t;
    int bits=TIOCM_DTR|TIOCM_RTS;
    d->fd=open(d->path,O_RDWR|O_NOCTTY|O_NONBLOCK);
    if (d->fd<0) { sr_err("Cannot open %s: %s",d->path,g_strerror(errno)); return SR_ERR_IO; }
    if (tcgetattr(d->fd,&t)<0) goto fail;
    t.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IXOFF|IXANY);
    t.c_oflag &= ~OPOST;
    t.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    t.c_cflag &= ~(CSIZE|PARENB|CSTOPB);
#ifdef CRTSCTS
    t.c_cflag &= ~CRTSCTS;
#endif
    t.c_cflag |= CS8|CLOCAL|CREAD;
    t.c_cc[VMIN]=0; t.c_cc[VTIME]=0;
    cfsetispeed(&t,B115200); cfsetospeed(&t,B115200);
    if (tcsetattr(d->fd,TCSANOW,&t)<0) goto fail;
    /* PTYs do not implement modem signals. USB CDC uses DTR for readiness. */
    if (ioctl(d->fd,TIOCMBIS,&bits)<0 && errno!=ENOTTY && errno!=EINVAL) goto fail;
    g_usleep(100000);
    tcflush(d->fd,TCIOFLUSH);
    d->tx_seq=d->rx_seq=0; memset(&d->parser,0,sizeof(d->parser));
    if (rp2350_command(d,LAR2_HELLO,NULL,0)!=SR_OK || rp2350_reply(d,LAR2_ID,2000)!=SR_OK) goto fail;
    if (lar2_u32(d->parser.buf+8)<9 || memcmp(d->parser.buf+12,"RP2350-LA",9)) goto fail;
    if (rp2350_command(d,LAR2_GET_CAPS,NULL,0)!=SR_OK || rp2350_reply(d,LAR2_CAPS,1000)!=SR_OK) goto fail;
    {
        const uint8_t *p=d->parser.buf+12;
        uint32_t len=lar2_u32(d->parser.buf+8), count;
        if (len<12 || p[0]!=8 || p[1]!=1 || lar2_u16(p+2)!=0) goto fail;
        d->max_samples=lar2_u32(p+4); count=lar2_u32(p+8);
        if (!d->max_samples || d->max_samples>LAR2_MAX_SAMPLES || !count || count>32 || len!=12+8*count) goto fail;
        for (uint32_t i=0;i<count;i++) {
            d->rates[i]=lar2_u64(p+12+8*i);
            if (!d->rates[i] || d->rates[i]>25000000) goto fail;
        }
        d->num_rates=count;
    }
    return SR_OK;
fail:
    sr_err("Cannot initialize LAR2 v1 device at %s.",d->path);
    close(d->fd); d->fd=-1; return SR_ERR;
}
