#include "videofilter.hpp"
#include "hwacc.hpp"
#include "mpimage.hpp"
#include "softwaredeinterlacer.hpp"
#include "deintoption.hpp"
#include "player/mpv_helper.hpp"
#include "opengl/opengloffscreencontext.hpp"
extern "C" {
#include <video/filter/vf.h>
#include <video/vdpau.h>
#include <video/vaapi.h>
#include <video/hwdec.h>
#include <video/mp_image_pool.h>
extern vf_info vf_info_noformat;
}

struct bomi_vf_priv {
    VideoFilter *vf;
    char *address, *swdec_deint, *hwdec_deint;
};

static auto priv(vf_instance *vf) -> VideoFilter*
{
    return reinterpret_cast<bomi_vf_priv*>(vf->priv)->vf;
}

auto create_vf_info() -> vf_info
{
#define MPV_OPTION_BASE bomi_vf_priv
    static m_option options[] = {
        MPV_OPTION(address),
        MPV_OPTION(swdec_deint),
        MPV_OPTION(hwdec_deint),
        mpv::null_option
    };

    static vf_info info;
    info.description = "bomi video filter";
    info.name = "noformat";
    info.open = VideoFilter::open;
    info.options = options;
    info.priv_size = sizeof(bomi_vf_priv);

    return info;
}

class HwDecTool {
public:
    HwDecTool() { m_pool = mp_image_pool_new(2); }
    virtual ~HwDecTool() { talloc_free(m_pool); }
    virtual auto download(const MpImage &src) -> MpImage = 0;
protected:
    mp_image_pool *m_pool = nullptr;
};

class VdpauTool: public HwDecTool {
public:
    VdpauTool(mp_vdpau_ctx *ctx): m_ctx(ctx) { }
    auto download(const MpImage &src) -> MpImage
    {
        auto img = MpImage::wrap(mp_image_pool_get(m_pool, IMGFMT_420P,
                                                   src->w, src->h));
        const VdpVideoSurface surface = (intptr_t)src->planes[3];
        if (m_ctx->vdp.video_surface_get_bits_y_cb_cr(
                    surface, VDP_YCBCR_FORMAT_YV12, (void* const*)img->planes,
                    (uint32_t*)img->stride) == VDP_STATUS_OK)
            return img;
        return MpImage();
    }
private:
    mp_vdpau_ctx *m_ctx = nullptr;
};

class VaApiTool : public HwDecTool {
public:
    VaApiTool(mp_vaapi_ctx *ctx): m_ctx(ctx) { }
    auto download(const MpImage &src) -> MpImage
    {
        auto img = va_surface_download((mp_image*)src.data(), m_pool);
        return img ? MpImage::wrap(img) : MpImage();
    }
private:
    mp_vaapi_ctx *m_ctx = nullptr;
};

vf_info vf_info_noformat = create_vf_info();

struct VideoFilter::Data {
    VideoFilter *p = nullptr;
    vf_instance *vf = nullptr;
    DeintOption deint_swdec, deint_hwdec;
    SoftwareDeinterlacer deinterlacer;
    mp_image_params params;
    bool deint = false, hwacc = false, inter_i = false, inter_o = false;
    OpenGLOffscreenContext *gl = nullptr;
    HwDecTool *hwdec = nullptr;
    mp_image_pool *pool = nullptr;

    QMutex mutex; // must be locked
    double ptsSkipStart = MP_NOPTS_VALUE, ptsLastSkip = MP_NOPTS_VALUE;
    bool skip = false;

    auto updateDeint() -> void
    {
        DeintOption opt;
        if (deint)
            opt = hwacc ? deint_hwdec : deint_swdec;
        deinterlacer.setOption(opt);
    //    d->vaapi.setDeintOption(opt);
        emit p->deintMethodChanged(opt.method);
    }
};

VideoFilter::VideoFilter()
    : d(new Data)
{
    d->p = this;
    d->pool = mp_image_pool_new(1);
}

VideoFilter::~VideoFilter()
{
    talloc_free(d->pool);
    delete d->hwdec;
    delete d;
}

auto VideoFilter::initializeGL(OpenGLOffscreenContext *ctx) -> void
{
    d->gl = ctx;
}

auto VideoFilter::finalizeGL() -> void
{
    d->gl = nullptr;
}

auto VideoFilter::open(vf_instance *vf) -> int
{
    auto priv = reinterpret_cast<bomi_vf_priv*>(vf->priv);
    priv->vf = address_cast<VideoFilter*>(priv->address);
    priv->vf->d->vf = vf;
    auto d = priv->vf->d;
    if (priv->swdec_deint)
        d->deint_swdec = DeintOption::fromString(_L(priv->swdec_deint));
    if (priv->hwdec_deint)
        d->deint_hwdec = DeintOption::fromString(_L(priv->hwdec_deint));
    d->updateDeint();
    memset(&d->params, 0, sizeof(d->params));
    vf->reconfig = reconfig;
    vf->filter_ext = filterIn;
    vf->filter_out = filterOut;
    vf->query_format = queryFormat;
    vf->uninit = uninit;
    vf->control = control;

    _Delete(d->hwdec);
    hwdec_request_api(vf->hwdec, HwAcc::name().toLatin1());
    if (vf->hwdec) {
        if (vf->hwdec->vdpau_ctx)
            d->hwdec = new VdpauTool(vf->hwdec->vdpau_ctx);
        else
            d->hwacc = new VaApiTool(vf->hwdec->vaapi_ctx);
    }
    mp_image_pool_clear(d->pool);
    priv->vf->stopSkipping();
    return true;
}

auto VideoFilter::isInputInterlaced() const -> bool
{
    return d->inter_i;
}

auto VideoFilter::isOutputInterlaced() const -> bool
{
    return d->inter_o;
}

auto VideoFilter::reconfig(vf_instance *vf,
                           mp_image_params *in, mp_image_params *out) -> int
{
    auto v = priv(vf); auto d = v->d;
    d->params = *in;
    *out = *in;
    if (_Change(d->hwacc, !!IMGFMT_IS_HWACCEL(in->imgfmt)))
        d->updateDeint();
    return 0;
}

auto VideoFilter::skipToNextBlackFrame() -> void
{
    d->mutex.lock();
    if (_Change(d->skip, true))
        emit scanningStarted();
    d->mutex.unlock();
}

auto VideoFilter::stopSkipping() -> void
{
    d->mutex.lock();
    if (_Change(d->skip, false))
        emit scanningFinished();
    d->ptsLastSkip = d->ptsSkipStart = MP_NOPTS_VALUE;
    d->mutex.unlock();
}

auto VideoFilter::isSkipping() const -> bool
{
    return d->skip;
}

template<class T, int bits>
static auto luma(const mp_image *mpi) -> double
{
    static_assert(sizeof(T)*8 >= bits, "!!!");
    constexpr const T Max = (1 << bits) - 1;
    static_assert(_Max<T>() == Max, "!!!");
    uchar *data = mpi->planes[0];
    double avg = 0;
    for (int y = 0; y < mpi->plane_h[0]; ++y) {
        const T *p = (const T*)(data + y * mpi->stride[0]);
        for (int x = 0; x < mpi->plane_w[0]; ++x)
            avg += *p++;
    }
    avg /= mpi->plane_h[0] * mpi->plane_w[0];
    avg /= Max;
    if (mpi->params.colorlevels == MP_CSP_LEVELS_TV)
        avg = (avg - 16.0/255)*255.0/(235.0 - 16.0);
    return avg;
}

static auto luminance(const mp_image *mpi) -> double
{
    uchar *data = mpi->planes[0];
    switch (mpi->imgfmt) {
    case IMGFMT_420P: {
        double avg = 0;
        for (int y = 0; y < mpi->plane_h[0]; ++y) {
            uchar *p = data + y * mpi->stride[0];
            for (int x = 0; x < mpi->plane_w[0]; ++x)
                avg += *p++;
        }
        avg /= mpi->plane_h[0] * mpi->plane_w[0];
        avg = (avg - 16)/(235 - 16);
        return avg;
    } case IMGFMT_420P9:
    case IMGFMT_420P10:
    case IMGFMT_420P12:
    case IMGFMT_420P14:
    case IMGFMT_420P16:

    default:
        return -1;
    }
}

auto VideoFilter::filterIn(vf_instance *vf, mp_image *_mpi) -> int
{
    if (!_mpi)
        return 0;
    auto v = priv(vf); Data *d = v->d;
    MpImage mpi = MpImage::wrap(_mpi);
    if (d->skip) {
        d->mutex.lock();
        auto scan = d->skip;
        auto start = d->ptsSkipStart;
        auto last = d->ptsLastSkip;
        d->mutex.unlock();
        if (scan) {
            auto skip = [&] () {
                if (mpi->pts == MP_NOPTS_VALUE)
                    return false;
                if (start == MP_NOPTS_VALUE)
                    start = mpi->pts;
                else {
                    if (mpi->pts < start)
                        return false;
                    if (mpi->pts - start > 10*60)// 10min
                        return false;
                }
                MpImage img;
                if (IMGFMT_IS_HWACCEL(mpi->imgfmt)) {
                    Q_ASSERT(d->hwdec);
                    if (!d->hwdec)
                        return false;
                    img = d->hwdec->download(mpi);
                } else
                    img = mpi;
                if (img.isNull())
                    return false;
                const auto y = luminance(img.data());
                if (y < 0.005)
                    return false;
                return true;
            };
            scan = skip();
            if (scan && qAbs(last - mpi->pts) > 0.0001) {
                d->mutex.lock();
                d->ptsLastSkip = mpi->pts;
                d->mutex.unlock();
            } else {
                v->stopSkipping();
                emit v->seekRequested(mpi->pts * 1000);
            }
            return 0;
        }
    }
    if (_Change(d->inter_i, mpi.isInterlaced()))
        emit v->inputInterlacedChanged();
    d->deinterlacer.push(std::move(mpi));
    return 0;
}

auto VideoFilter::filterOut(vf_instance *vf) -> int
{
    auto v = priv(vf); auto d = v->d;
    auto mpi = std::move(d->deinterlacer.pop());
    if (mpi.isNull())
        return 0;
    if (_Change(d->inter_o, d->deinterlacer.pass() ? d->inter_i : false))
        emit v->outputInterlacedChanged();
    vf_add_output_frame(vf, mpi.take());

    return 0;
}

auto VideoFilter::control(vf_instance *vf, int request, void* data) -> int
{
    auto v = priv(vf); auto d = v->d;
    switch (request){
    case VFCTRL_GET_DEINTERLACE:
        *(int*)data = d->deint;
        return true;
    case VFCTRL_SET_DEINTERLACE:
        if (_Change(d->deint, (bool)*(int*)data))
            d->updateDeint();
        return true;
    default:
        return CONTROL_UNKNOWN;
    }
}

auto VideoFilter::uninit(vf_instance *vf) -> void {
    auto v = priv(vf); auto d = v->d;
    d->deinterlacer.clear();
}

auto query_video_format(quint32 format) -> int
{
    switch (format) {
    case IMGFMT_VDPAU:     case IMGFMT_VDA:       case IMGFMT_VAAPI:
    case IMGFMT_420P:      case IMGFMT_444P:
    case IMGFMT_420P16:
    case IMGFMT_420P14:
    case IMGFMT_420P12:
    case IMGFMT_420P10:
    case IMGFMT_420P9:
    case IMGFMT_NV12:      case IMGFMT_NV21:
    case IMGFMT_YUYV:      case IMGFMT_UYVY:
    case IMGFMT_BGRA:      case IMGFMT_RGBA:
    case IMGFMT_ARGB:      case IMGFMT_ABGR:
    case IMGFMT_BGR0:      case IMGFMT_RGB0:
    case IMGFMT_0RGB:      case IMGFMT_0BGR:
        return VFCAP_CSP_SUPPORTED | VFCAP_CSP_SUPPORTED_BY_HW;
    default:
        return 0;
    }
}

auto VideoFilter::queryFormat(vf_instance *vf, uint fmt) -> int
{
    if (query_video_format(fmt))
        return vf_next_query_format(vf, fmt);
    return false;
}
