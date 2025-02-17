// Copyright 2002 - 2008, 2010, 2011 National Technology Engineering
// Solutions of Sandia, LLC (NTESS). Under the terms of Contract
// DE-NA0003525 with NTESS, the U.S. Government retains certain rights
// in this software.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <Akri_Eikonal_Calc.hpp>
#include <stk_mesh/base/Entity.hpp>
#include <Akri_FieldRef.hpp>
#include <Akri_Vec.hpp>
#include <Akri_DiagWriter.hpp>
#include <Akri_MeshHelpers.hpp>

namespace krino {

double calculate_gradient_magnitude_triangle(const std::array<Vector3d,3> & x, const std::array<double,3> & d)
{
  const double d10 = d[1] - d[0];
  const double d20 = d[2] - d[0];
  const Vector3d x10 = x[1] - x[0];
  const Vector3d x20 = x[2] - x[0];

  const double detJ = (x10[0]*x20[1]-x20[0]*x10[1]);
  const Vector3d grad = d10*Vector3d(x20[1],-x20[0],0.0) + d20*Vector3d(-x10[1],x10[0],0.0);
  return grad.length()/detJ;
}

double calculate_gradient_magnitude_tetrahedron(const std::array<Vector3d,4> & x, const std::array<double,4> & d)
{
  const double d10 = d[1] - d[0];
  const double d20 = d[2] - d[0];
  const double d30 = d[3] - d[0];
  const Vector3d x10 = x[1] - x[0];
  const Vector3d x20 = x[2] - x[0];
  const Vector3d x30 = x[3] - x[0];

  const Vector3d x10_x_x20 = Cross(x10,x20);
  const Vector3d x20_x_x30 = Cross(x20,x30);
  const Vector3d x30_x_x10 = Cross(x30,x10);

  const double detJ = Dot(x30,x10_x_x20);
  const Vector3d grad = d10*x20_x_x30 + d20*x30_x_x10 + d30*x10_x_x20;
  return grad.length()/detJ;
}

double calculate_gradient_magnitude(const int npe,
    const stk::mesh::Entity * elem_nodes,
    const FieldRef dRef,
    const std::function<const Vector3d &(stk::mesh::Entity)> & get_coordinates)
{
  double mag_grad = 1.0;

  if (3 == npe)
  {
    const std::array<Vector3d,3> x{get_coordinates(elem_nodes[0]), get_coordinates(elem_nodes[1]), get_coordinates(elem_nodes[2])};
    const std::array<double,3> d{*field_data<double>(dRef, elem_nodes[0]), *field_data<double>(dRef, elem_nodes[1]), *field_data<double>(dRef, elem_nodes[2])};
    mag_grad = calculate_gradient_magnitude_triangle(x,d);
  }
  else
  {
    ThrowAssert(4 == npe);

    const std::array<Vector3d,4> x{get_coordinates(elem_nodes[0]), get_coordinates(elem_nodes[1]), get_coordinates(elem_nodes[2]), get_coordinates(elem_nodes[3])};
    const std::array<double,4> d{*field_data<double>(dRef, elem_nodes[0]), *field_data<double>(dRef, elem_nodes[1]), *field_data<double>(dRef, elem_nodes[2]), *field_data<double>(dRef, elem_nodes[3])};
    mag_grad = calculate_gradient_magnitude_tetrahedron(x,d);
  }

  return mag_grad;
}

double calculate_gradient_magnitude(const int npe,
    const stk::mesh::Entity * elem_nodes,
    const FieldRef dRef,
    const FieldRef xRef)
{
  double mag_grad = 1.0;

  if (3 == npe)
  {
    const std::array<Vector3d,3> x{Vector3d(field_data<double>(xRef, elem_nodes[0]),2), Vector3d(field_data<double>(xRef, elem_nodes[1]),2), Vector3d(field_data<double>(xRef, elem_nodes[2]),2)};
    const std::array<double,3> d{*field_data<double>(dRef, elem_nodes[0]), *field_data<double>(dRef, elem_nodes[1]), *field_data<double>(dRef, elem_nodes[2])};
    mag_grad = calculate_gradient_magnitude_triangle(x,d);
  }
  else
  {
    ThrowAssert(4 == npe);

    const std::array<Vector3d,4> x{Vector3d(field_data<double>(xRef, elem_nodes[0])), Vector3d(field_data<double>(xRef, elem_nodes[1])), Vector3d(field_data<double>(xRef, elem_nodes[2])), Vector3d(field_data<double>(xRef, elem_nodes[3]))};
    const std::array<double,4> d{*field_data<double>(dRef, elem_nodes[0]), *field_data<double>(dRef, elem_nodes[1]), *field_data<double>(dRef, elem_nodes[2]), *field_data<double>(dRef, elem_nodes[3])};
    mag_grad = calculate_gradient_magnitude_tetrahedron(x,d);
  }

  return mag_grad;
}

std::array<int,3> get_oriented_nodes_triangle(const int nodeToUpdate)
{
  const std::array<int,3> lnn{(nodeToUpdate + 1) % 3, (nodeToUpdate + 2) % 3, nodeToUpdate};
  return lnn;
}

std::array<int,4> get_oriented_nodes_tetrahedron(const int nodeToUpdate)
{
  static const std::array<std::array<int,4>,4> nodePermute{{ {{1,3,2,0}}, {{0,2,3,1}}, {{0,3,1,2}}, {{0,1,2,3}} }};

  const std::array<int,4> lnn {nodePermute[nodeToUpdate][0], nodePermute[nodeToUpdate][1], nodePermute[nodeToUpdate][2], nodePermute[nodeToUpdate][3]};
  return lnn;
}

static double eikonal_solve_triangle_2d_or_3d(const std::array<Vector3d,3> & x, const std::array<double,2> & d, const int dim, const double far, const double speed)
{
  if (d[0] == far || d[1] == far)
  {
    return far;
  }

  const double sqrSpeed = speed*speed;
  const double d10 = d[1] - d[0];
  const Vector3d x10 = x[1] - x[0];
  const Vector3d x20 = x[2] - x[0];
  const double sqr10 = x10.length_squared();
  const double sqr20 = x20.length_squared();

  double detJ = 0;
  if (2 == dim)
  {
    detJ = x10[0]*x20[1]-x20[0]*x10[1];
  }
  else // (3 == dim)
  {
    ThrowAssert(3 == dim);
    detJ = Cross(x10,x20).length();
  }
  ThrowAssert(detJ > 0.0);

  const double a = sqr10;
  const double b = -2.0 * d10 * Dot(x10,x20);
  const double c = d10*d10 * sqr20 - detJ*detJ/sqrSpeed;

  const double det = b*b-4.0*a*c;
  if (det > 0.0)
  {
    // solve quadratic equation, roots are q/a and c/q
    const int sign_b = ( b < 0.0 ) ? -1 : 1;
    const double q = -0.5*(b + sign_b*std::sqrt(det));

    const double d20 = std::max(c/q,q/a);

    const double loc = (Dot(x10,x20) - sqrSpeed*d10*d20) / (sqr10 - sqrSpeed*d10*d10);

    if (loc >= 0.0 && loc <= 1.0)
      return d[0] + d20;
  }

  const double h20 = std::sqrt(sqr20);
  const double h21 = (x[2] - x[1]).length();
  return std::min(d[0]+h20/speed,d[1]+h21/speed);
}

double eikonal_solve_triangle(const std::array<Vector3d,3> & x, const std::array<double,2> & dSigned, const int sign, const double far, const double speed)
{
  const std::array<double,2> d{{ std::abs(dSigned[0]), std::abs(dSigned[1]) }};
  static constexpr int dim = 2;
  return sign*eikonal_solve_triangle_2d_or_3d(x, d, dim, far, speed);
}

double eikonal_solve_triangle3d(const std::array<Vector3d,3> & x, const std::array<double,2> & dSigned, const double far, const double speed)
{
  const std::array<double,2> d{{ std::abs(dSigned[0]), std::abs(dSigned[1]) }};
  static constexpr int dim = 3;
  return eikonal_solve_triangle_2d_or_3d(x, d, dim, far, speed);
}

double eikonal_solve_tetrahedron_side(const std::array<Vector3d,4> & x, const std::array<double,3> & d, const double far, const double speed, const int iSide)
{
  static const std::array<std::array<int,3>,3> sides{{ {{0,1,3}}, {{1,2,3}}, {{2,0,3}} }};
  const std::array<int,3> & sideLNN = sides[iSide];
  const std::array<Vector3d,3> xSide{x[sideLNN[0]],x[sideLNN[1]],x[sideLNN[2]]};
  const std::array<double,2> dSide{d[sideLNN[0]],d[sideLNN[1]]};
  return eikonal_solve_triangle3d(xSide,dSide,far,speed);
}

double eikonal_solve_tetrahedron(const std::array<Vector3d,4> & x, const std::array<double,3> & dSigned, const int sign, const double far, const double speed)
{
  const std::array<double,3> d{{ std::abs(dSigned[0]), std::abs(dSigned[1]), std::abs(dSigned[2]) }};
  return sign*eikonal_solve_tetrahedron(x, d, far, speed);
}

double eikonal_solve_tetrahedron(const std::array<Vector3d,4> & x, const std::array<double,3> & d, const double far, const double speed)
{
  if (d[0] == far || d[1] == far || d[2] == far)
  {
    return far;
  }

  const double sqrSpeed = speed*speed;
  const double d10 = d[1]-d[0];
  const double d20 = d[2]-d[0];

  const Vector3d x10 = x[1] - x[0];
  const Vector3d x20 = x[2] - x[0];
  const Vector3d x30 = x[3] - x[0];

  const Vector3d x10_x_x20 = Cross(x10,x20);
  const Vector3d x20_x_x30 = Cross(x20,x30);
  const Vector3d x30_x_x10 = Cross(x30,x10);

  const double detJ = Dot(x30,x10_x_x20);
  ThrowAssert(detJ > 0);

  const Vector3d contrib12 = d10*x20_x_x30 + d20*x30_x_x10;

  const double a = x10_x_x20.length_squared();
  const double b = 2.0 * Dot(x10_x_x20,contrib12);
  const double c = contrib12.length_squared() - detJ*detJ/sqrSpeed;

  const double det = b*b-4.0*a*c;
  if (det > 0.0)
  {
    // solve quadratic equation, roots are q/a and c/q
    const int sign_b = ( b < 0.0 ) ? -1 : 1;
    const double q = -0.5*(b + sign_b*std::sqrt(det));

    const double d30 = std::max(c/q,q/a);

    // Solve 2x2 system for parametric coords of intersection of gradient and 0-1-2
    // A1 r + B1 s == C1
    // A2 r + B2 s == C2
    const double A1 = x10.length_squared() - sqrSpeed*d10*d10;
    const double B1 = Dot(x10,x20) - sqrSpeed*d10*d20;
    const double C1 = Dot(x10,x30) - sqrSpeed*d10*d30;
    const double A2 = B1;
    const double B2 = x20.length_squared() - sqrSpeed*d20*d20;
    const double C2 = Dot(x20,x30) - sqrSpeed*d20*d30;
    const double denom = A2*B1 - A1*B2;
    const double loc_r = (C2*B1 - C1*B2)/denom;
    const double loc_s = (A2*C1 - A1*C2)/denom;
    const double loc_t = 1.0 - loc_r - loc_s;

    if (loc_r >= 0.0 && loc_s >= 0.0 && loc_t >= 0.0)
      return d[0] + d30;
  }

  return std::min({
    eikonal_solve_tetrahedron_side(x,d,far,speed,0),
    eikonal_solve_tetrahedron_side(x,d,far,speed,1),
    eikonal_solve_tetrahedron_side(x,d,far,speed,2)});
}
}
