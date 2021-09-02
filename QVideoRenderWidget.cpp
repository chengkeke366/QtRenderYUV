#include "QVideoRenderWidget.h"
#include <QWeakPointer>
#include <QPointer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QTimer>

#include <thread>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstdint>

#define GLVERSION  "#version 330 core\n"
#define  GET_SHADER(arg) GLVERSION#arg

const char* vertex_shader = GET_SHADER(
	layout (location = 0) in vec3 aPos;
    layout (location = 1) in vec2 aTexCoord;

    out vec2 TexCoord;

	void main()
	{
		gl_Position = vec4(aPos, 1.0);
		TexCoord = aTexCoord;
	}
);

const char* frag_shader = GET_SHADER(
	out vec4 FragColor;
    in vec2 TexCoord;
    uniform sampler2D texY;
    uniform sampler2D texU;
    uniform sampler2D texV;

	void main()
	{
		vec3 rgb;
		vec3 yuv;
		yuv.x = texture(texY, TexCoord).r - 0.0625;
		yuv.y = texture(texU, TexCoord).r - 0.5;
		yuv.z = texture(texV, TexCoord).r - 0.5;

		rgb = mat3(1.164, 1.164,    1.164,
				   0.0,   -0.213,   2.114,
			       1.792, -0.534,   0.0) * yuv;

		FragColor = vec4(rgb, 1.0);
	}
);

QVideoRenderWidget::QVideoRenderWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{

}

QVideoRenderWidget::~QVideoRenderWidget()
{
	// Make sure the context is current and then explicitly
	 // destroy all underlying OpenGL resources.
	makeCurrent();
    m_vao->destroy();
    m_vbo_yuv->destroy();
    delete m_vbo_yuv;
    delete m_vao;

    delete m_shaderProgram;
    doneCurrent();
}

void QVideoRenderWidget::setTextureI420PData(uint8_t* Buffer[3],int Stride[3], int width, int height)
{
    if (m_width!=width || m_height!=height)
    {
		makeCurrent();//If you need to call the standard OpenGL API functions from other places (e.g. in your widget's constructor or in your own paint functions), you must call makeCurrent() first.
//����yuv ����
		for (int i = 0; i < 3; i++)
		{
			m_texture_2d_array[i] = QSharedPointer<QOpenGLTexture>( new QOpenGLTexture(QOpenGLTexture::Target2D));
			if (i == 0)
			{
				m_texture_2d_array[i]->setSize(width, height);
			}
			else
			{
				m_texture_2d_array[i]->setSize(width / 2, height / 2);
			}
			m_texture_2d_array[i]->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
			m_texture_2d_array[i]->create();
			m_texture_2d_array[i]->setFormat(QOpenGLTexture::R8_UNorm);
			m_texture_2d_array[i]->allocateStorage();
		}
		doneCurrent();
    }

    m_yTexture_data = std::shared_ptr<uint8_t[]>(new uint8_t[width * height], std::default_delete<uint8_t[]>());
    m_uTexture_data = std::shared_ptr<uint8_t[]>(new uint8_t[width * height/4], std::default_delete<uint8_t[]>());
	m_vTexture_data = std::shared_ptr<uint8_t[]>(new uint8_t[width * height/4], std::default_delete<uint8_t[]>());

    //copy y
    const uint8_t* p_ydata = Buffer[0];
    uint8_t* ypoint = m_yTexture_data.get();
    for (int i=0; i<height; i++)
    {
        memcpy(ypoint, p_ydata, width);
        p_ydata = p_ydata + Stride[0];
        ypoint = ypoint + width;
    }

    // copy u
    const uint8_t* p_udata = Buffer[1];
    uint8_t* upoint = m_uTexture_data.get();
	for (int i = 0; i < height/2; i++)
	{
		memcpy(upoint, p_udata, width/2);
        p_udata = p_udata + Stride[1];
        upoint = upoint + width / 2;
	}

    // copy v
    const uint8_t* p_vdata = Buffer[2];
    uint8_t* vpoint = m_vTexture_data.get();
	for (int i = 0; i < height/2; i++)
	{
		memcpy(vpoint, p_vdata, width/2);
        p_vdata = p_vdata + Stride[2];
        vpoint = vpoint + width / 2;
	}
    update();
}

void QVideoRenderWidget::clearTextureColor()
{
    m_yTexture_data.reset();
    m_uTexture_data.reset();
    m_vTexture_data.reset();
    update();
}

void QVideoRenderWidget::initializeGL()
{
    initializeOpenGLFunctions();//Initializes OpenGL function resolution for the current context.

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);    //���ñ���ɫΪ��ɫ

    //makeCurrent();//ʹ�õ�ǰ���ڵ�������
    
    //����shader program
    m_shaderProgram = new QOpenGLShaderProgram(this);
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader);
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, frag_shader);

    m_shaderProgram->link();

    float vertices[] = {
        //��������                    ��������
        -1.0f, -1.0f, 0.0f,           0.0f, 1.0f, //���½�
         1.0f, -1.0f, 0.0f,           1.0f, 1.0f, //���½�
        -1.0f,  1.0f, 0.0f,           0.0f, 0.0f, //���Ͻ�
         1.0f,  1.0f, 0.0f,           1.0f, 0.0f  //���Ͻ�
    };
    //����VAO��VBO
    m_vao = new QOpenGLVertexArrayObject(this);
    m_vao->create();
    m_vao->bind();
	
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);//�ǳ��ؼ������ö��롣���stride�����ڿ�ȣ���Ҫ����gl���ݵĶ��뷽ʽ��������ܳ���UV��ɫ��Y�޷��ص�������

    m_vbo_yuv = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    m_vbo_yuv->create();//�൱��glGenBuffer
    m_vbo_yuv->bind();  //�൱��glBindBuffer
    m_vbo_yuv->allocate(vertices, sizeof(vertices));//�൱��glBufferData

    //����OpenGL��ζ�ȡ���㼰����:���ȵõ����Ե�location��Ȼ��ָ��ÿ��location��[�������͡����ȡ���ȡ��������ʼƫ����]���ĸ�����

    // ����ڶ�����ɫ����û��ָ��layout(location=) ���˵���������ʹ�ø÷�����ȡ�����Ե�location
     uint32_t aPos = m_shaderProgram->attributeLocation("aPos"); 
     uint32_t aTexCoord = m_shaderProgram->attributeLocation("aTexCoord");
    
    //����Shader����������ζ�ȡ���൱�ڵ�����glVertexAttribPointer���ⲿִ���ֻ꣬�Ǹ���GPU��ζ�ȡ������Ҫ�����Ӧ�������ԣ�����GPU�����޷���ȡ�����ݵ�
    m_shaderProgram->setAttributeBuffer(aPos,      GL_FLOAT,       0,           3, sizeof(float) * 5);
    //����Shader����������ζ�ȡ���൱�ڵ�����glVertexAttribPointer
    m_shaderProgram->setAttributeBuffer(aTexCoord, GL_FLOAT, 3*sizeof(float),   2, sizeof(float) * 5);

    //ͨ��location�����Ӧ�Ķ������ԣ���ôGPU���ܶ�ȡ��Ӧ�������ԡ�Ĭ������¼�ʹ
    m_shaderProgram->enableAttributeArray(aPos);
    m_shaderProgram->enableAttributeArray(aTexCoord);

    //��ʱ�Ѿ������VBO��VAO�󶨣����Զ�VBO��VAO���н����
    m_vao->release();
    m_vbo_yuv->release();
}

void QVideoRenderWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void QVideoRenderWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);//��ȾglClearColor���õ���ɫ
    m_shaderProgram->bind();
    m_vao->bind();
	if (m_yTexture_data.use_count()!=0 && m_uTexture_data.use_count() != 0 && m_vTexture_data.use_count() != 0)
	{
		//����OpenGLÿ����ɫ�������������ĸ�����Ԫ���൱�ڵ��� glUniform1i(glGetUniformLocation(ourShader.ID, "texture1"), 0);
		m_shaderProgram->setUniformValue("texY", 0);
		m_shaderProgram->setUniformValue("texU", 1);
		m_shaderProgram->setUniformValue("texV", 2);

		//����YUV���ݵ�������

		m_texture_2d_array[0]->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, m_yTexture_data.get());
		m_texture_2d_array[0]->bind(0);//��������
		m_texture_2d_array[1]->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, m_uTexture_data.get());
		m_texture_2d_array[1]->bind(1);
		m_texture_2d_array[2]->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, m_vTexture_data.get());
		m_texture_2d_array[2]->bind(2);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);//ʹ��VAO���л���

		m_texture_2d_array[0]->release();
		m_texture_2d_array[1]->release();
		m_texture_2d_array[2]->release();
	}
    
    m_vao->release();
    m_shaderProgram->release();
}

