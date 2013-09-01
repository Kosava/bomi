#ifndef VIDEOTEXTURESHADER_HPP
#define VIDEOTEXTURESHADER_HPP

#include "videorendereritem.hpp"
#include "colorproperty.hpp"
#include "videoframe.hpp"

struct Kernel3x3 {
	Kernel3x3() { mat(0, 0) = mat(2, 2) = 0.f; }
	Kernel3x3(double center, double neighbor, double diagonal) {
		set(center, neighbor, diagonal);
	}
	Kernel3x3 &operator = (const Kernel3x3 &rhs) { mat = rhs.mat; return *this; }
	float &operator () (int i, int j) { return at(i, j); }
	float  operator () (int i, int j) const {return at(i, j); }
	Kernel3x3 &operator += (const Kernel3x3 &rhs) { mat += rhs.mat; return *this; }
	Kernel3x3 operator + (const Kernel3x3 &rhs) { Kernel3x3 lhs = *this; return (lhs += rhs); }

	void normalize() {
		double den = 0.0;
		for (int i=0; i<9; ++i)
			den += mat.data()[i];
		mat /= den;
	}
	Kernel3x3 normalized() const {
		Kernel3x3 kernel = *this;
		kernel.normalize();
		return kernel;
	}
	float &at(int i, int j) { return mat(i, j); }
	float at(int i, int j) const {return mat(i, j); }
	void set(double center, double neighbor, double diagonal) {
		mat(1, 1) = center;
		mat(0, 1) = mat(1, 0) = mat(1, 2) = mat(2, 1) = neighbor;
		mat(0, 0) = mat(0, 2) = mat(2, 0) = mat(2, 2) = diagonal;
	}
	float center() const {return mat(1, 1);}
	float neighbor() const {return mat(0, 1);}
	float diagonal() const {return mat(0, 0);}
	QMatrix3x3 matrix() const {return mat;}
private:
	QMatrix3x3 mat;
};

struct VideoTextureInfo {
	GLuint id = GL_NONE; int plane = 0, width = 0, height = 0; GLenum target = GL_TEXTURE_2D, format = GL_NONE, type = GL_UNSIGNED_BYTE, internal = GL_NONE;
};

struct VideoTextureUploader {
	virtual ~VideoTextureUploader() {}
	virtual void initialize(const VideoTextureInfo &info) {
		glBindTexture(info.target, info.id);
		glTexParameterf(info.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(info.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(info.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(info.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(info.target, 0, info.internal, info.width, info.height, 0, info.format, info.type, nullptr);
	}
	virtual void upload(const VideoTextureInfo &info, const VideoFrame &frame) {
		glBindTexture(info.target, info.id);
		glTexSubImage2D(info.target, 0, 0, 0, info.width, info.height, info.format, info.type, frame.data(info.plane));
	}
	virtual QImage toImage(const VideoFrame &frame) const {
		return frame.toImage();
	}
	virtual mp_csp colorspace(const VideoFormat &format) const { return format.colorspace(); }
	virtual mp_csp_levels colorrange(const VideoFormat &format) const { return format.range(); }
};

class VideoTextureShader {
public:
	static constexpr int KernelEffects = VideoRendererItem::KernelEffects;
	static VideoTextureShader *create(const VideoFormat &format, const ColorProperty &color = ColorProperty(), int effects = 0);
	VideoTextureShader(const VideoFormat &format, GLenum target = GL_TEXTURE_2D);
	virtual ~VideoTextureShader() { delete m_uploader; }
	void initialize(GLuint *textures);
	GLuint *textures() const {return m_textures;}
	const VideoFormat &format() const {return m_format;}
	QByteArray fragment() const;
	void getCoords(double &x1, double &y1, double &x2, double &y2) const { m_rect.getCoords(&x1, &y1, &x2, &y2); }
	void link(QOpenGLShaderProgram *program);
	void render(QOpenGLShaderProgram *program, const Kernel3x3 &kernel);
	virtual void upload(const VideoFrame &frame);
	QImage toImage(const VideoFrame &frame) const {return m_uploader->toImage(frame);}
	void setColor(const ColorProperty &color) { m_color = color; updateMatrix(); }
	bool setEffects(int effects);
	void upload(const QImage &image);
	bool hasKernelEffects() const {return m_effects & KernelEffects;}
protected:
	void setTexel(const QByteArray &codes) {m_texel = codes;}
	QPointF &sc(int idx) {return *m_strideCorrection[idx];}
	QPointF sc(int idx) const {return *m_strideCorrection[idx];}
	void addTexInfo(int plane, int width, int height, GLenum format, GLenum type = GL_UNSIGNED_BYTE, GLenum internal = GL_NONE) {
		VideoTextureInfo info;
		info.plane = plane; info.width = width; info.height = height;			info.target = m_target;
		info.format = format; info.type = type; info.internal = (internal == GL_NONE) ? format : internal;
		m_info.append(info);
	}
	void addRgbInfo(int plane, int width, int height, GLenum format, GLenum type = GL_UNSIGNED_INT_8_8_8_8_REV) {
		addTexInfo(plane, width, height, format, type, 4);
	}
private:
	void updateMatrix();
	void setUploader(VideoTextureUploader *uploader) { if (m_uploader) delete m_uploader; m_uploader = uploader; }
	VideoFormat m_format;
	GLuint *m_textures = nullptr;
	GLenum m_target = GL_TEXTURE_2D;
	QByteArray m_header;
	// texel correction for second and third planar. occasionally, the stride does not match exactly, e.g, for I420, stride[1]*2 != stride[0]
	QPointF m_sc1 = {1.0, 1.0}, m_sc2 = {1.0, 1.0}, m_sc3 = {1.0, 1.0};
	QPointF *m_strideCorrection[3] = {&m_sc1, &m_sc2, &m_sc3};
	int loc_kern_d, loc_kern_c, loc_kern_n;
	int loc_p1, loc_p2, loc_p3;
	int loc_sc1, loc_sc2, loc_sc3, loc_conv_mat, loc_conv_vec, loc_orig_vec;
	QRectF m_rect = {0, 0, 1, 1};
	QVector<VideoTextureInfo> m_info;
	VideoTextureUploader *m_uploader = nullptr;
	QMatrix3x3 m_conv_mat;
	QVector3D m_conv_vec;
	QVector3D m_orig_vec;
	QByteArray m_texel = "vec3 texel(const in vec2 coord) {return texture1(coord).xyz;}";
	ColorProperty m_color;
	int m_effects = 0;
};

#endif // TEXTURESHADER_HPP