//==================================================
// point_drawer.h
//
//  Copyright (c) 2016 Benjamin Hepp.
//  Author: Benjamin Hepp
//  Created on: Dec 8, 2016
//==================================================
#pragma once

#include <array>
#include <QtOpenGL>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>

struct OGLVertexData
{
  OGLVertexData()
  : x(0), y(0), z(0) {}

  OGLVertexData(float x, float y, float z)
  : x(x), y(y), z(z) {}

  OGLVertexData& operator=(const OGLVertexData& other) {
    x = other.x;
    y = other.y;
    z = other.z;
    return *this;
  }

  OGLVertexData operator+() const {
    return OGLVertexData(x, y, z);
  }

  OGLVertexData operator-() const {
    return OGLVertexData(-x, -y, -z);
  }

  float x;
  float y;
  float z;
};

struct OGLColorData
{
  OGLColorData()
  : r(0), g(0), b(0), a(1) {}

  OGLColorData(float r, float g, float b)
  : r(r), g(g), b(b), a(1) {}

  OGLColorData(float r, float g, float b, float a)
  : r(r), g(g), b(b), a(a) {}

  OGLColorData& operator=(const OGLColorData& other) {
    r = other.r;
    g = other.g;
    b = other.b;
    a = other.a;
    return *this;
  }

  float r;
  float g;
  float b;
  float a;
};

inline std::ostream& operator<<(std::ostream& out, const OGLVertexData& vertex) {
  out << "(" << vertex.x << ", " << vertex.y << ", " << vertex.z << ")";
  return out;
}

struct OGLVertexDataRGBA
{
  OGLVertexDataRGBA()
    : x(0), y(0), z(0), r(0), g(0), b(0), a(0) {}

    OGLVertexDataRGBA(float x, float y, float z)
    : x(x), y(y), z(z), r(0), g(0), b(0), a(0) {}

    OGLVertexDataRGBA(float x, float y, float z, float r, float g, float b, float a)
    : x(x), y(y), z(z), r(r), g(g), b(b), a(a) {}

    OGLVertexDataRGBA(const OGLVertexData& vertex, const OGLColorData& color)
    : x(vertex.x), y(vertex.y), z(vertex.z),
      r(color.r), g(color.g), b(color.b), a(color.a) {}

    OGLVertexDataRGBA(const OGLVertexDataRGBA& other)
    : x(other.x), y(other.y), z(other.z),
      r(other.r), g(other.g), b(other.b), a(other.a) {}

    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
};

inline std::ostream& operator<<(std::ostream& out, const OGLVertexDataRGBA& vertex) {
    out << "(" << vertex.x << ", " << vertex.y << ", " << vertex.z << ")";
    return out;
}

class PointDrawer
{
public:
    PointDrawer()
    : num_points_(0) {
    }

    ~PointDrawer() {
        clear();
    }

    void clear() {
        num_points_ = 0;
        if (vao_.isCreated()) {
            vao_.destroy();
        }
        if (vbo_.isCreated()) {
            vbo_.destroy();
        }
    }

    void init() {
        clear();

        if (program_.isLinked()) {
            program_.release();
            program_.removeAllShaders();
        }

        program_.create();
        program_.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/points.v.glsl");
        program_.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/points.f.glsl");
        program_.link();

        vao_.create();
        vbo_.create();
    }

    void upload(const std::vector<OGLVertexDataRGBA>& point_data) {
        num_points_ = point_data.size();
        program_.bind();
        vao_.bind();
        vbo_.bind();

        vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        vbo_.allocate(point_data.data(), point_data.size() * sizeof(OGLVertexDataRGBA));

        program_.enableAttributeArray(0);
        program_.setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(OGLVertexDataRGBA));


        program_.enableAttributeArray(1);
        program_.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 4, sizeof(OGLVertexDataRGBA));

        vbo_.release();
        vao_.release();
        program_.release();
    }

    void draw(const QMatrix4x4& pvm_matrix, const float point_size) {
        if (num_points_ == 0) {
            return;
        }

        program_.bind();
        vao_.bind();

        program_.setUniformValue("u_pvm_matrix", pvm_matrix);
        program_.setUniformValue("u_point_size", point_size);

        glDrawArrays(GL_POINTS, 0, num_points_);

        vao_.release();
        program_.release();
    }

private:
    size_t num_points_;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_;
    QOpenGLShaderProgram program_;
};
