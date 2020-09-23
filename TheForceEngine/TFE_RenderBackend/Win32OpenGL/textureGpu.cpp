#include <TFE_RenderBackend/textureGpu.h>
#include <GL/glew.h>
#include <vector>

static std::vector<u8> s_workBuffer;

TextureGpu::~TextureGpu()
{
	if (m_gpuHandle)
	{
		glDeleteTextures(1, &m_gpuHandle);
		m_gpuHandle = 0;
	}
}

bool TextureGpu::create(u32 width, u32 height)
{
	m_width = width;
	m_height = height;

	glGenTextures(1, &m_gpuHandle);
	if (!m_gpuHandle) { return false; }

	glBindTexture(GL_TEXTURE_2D, m_gpuHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

bool TextureGpu::createWithData(u32 width, u32 height, const void* buffer, MagFilter magFilter)
{
	m_width = width;
	m_height = height;

	glGenTextures(1, &m_gpuHandle);
	if (!m_gpuHandle) { return false; }

	glBindTexture(GL_TEXTURE_2D, m_gpuHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	
	f32 maxAniso = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter == MAG_FILTER_NONE ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, maxAniso);

	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

bool TextureGpu::update(const void* buffer, size_t size)
{
	if (size < m_width * m_height * 4) { return false; }

	glBindTexture(GL_TEXTURE_2D, m_gpuHandle);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	glBindTexture(GL_TEXTURE_2D, 0);

	return true;
}

void TextureGpu::bind(u32 slot/* = 0*/) const
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, m_gpuHandle);
}

void TextureGpu::clear(u32 slot/* = 0*/)
{
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, 0);
}
