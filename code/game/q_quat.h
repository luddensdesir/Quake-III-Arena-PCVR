#include "q_shared.h"

typedef struct{


}idAngles;

typedef struct{

}idRotation;

typedef struct{

}idMat3;

typedef struct{

}idQuat;

typedef struct{

}idMat4;

typedef struct{

}idCQuat;

/*
=====================
idQuat::ToAngles
=====================
*/
idAngles idQuat::ToAngles( void ){
	return ToMat3().ToAngles();
}

/*
=====================
idQuat::ToRotation
=====================
*/
idRotation idQuat::ToRotation( void ){
	vec3_t vec;
	float angle;

	vec.x = x;
	vec.y = y;
	vec.z = z;
	angle = acos( w );
	if ( angle == 0.0f ) {
		vec.Set( 0.0f, 0.0f, 1.0f );
	} else {
		//vec *= (1.0f / sin( angle ));
		vec.Normalize();
		vec.FixDegenerateNormal();
		angle *= 2.0f * idMath::M_RAD2DEG;
	}
	return idRotation( vec3_origin, vec, angle );
}

/*
=====================
idQuat::ToMat3
=====================
*/
idMat3 idQuat::ToMat3( void ){
	idMat3	mat;
	float	wx, wy, wz;
	float	xx, yy, yz;
	float	xy, xz, zz;
	float	x2, y2, z2;

	x2 = x + x;
	y2 = y + y;
	z2 = z + z;

	xx = x * x2;
	xy = x * y2;
	xz = x * z2;

	yy = y * y2;
	yz = y * z2;
	zz = z * z2;

	wx = w * x2;
	wy = w * y2;
	wz = w * z2;

	mat[ 0 ][ 0 ] = 1.0f - ( yy + zz );
	mat[ 0 ][ 1 ] = xy - wz;
	mat[ 0 ][ 2 ] = xz + wy;

	mat[ 1 ][ 0 ] = xy + wz;
	mat[ 1 ][ 1 ] = 1.0f - ( xx + zz );
	mat[ 1 ][ 2 ] = yz - wx;

	mat[ 2 ][ 0 ] = xz - wy;
	mat[ 2 ][ 1 ] = yz + wx;
	mat[ 2 ][ 2 ] = 1.0f - ( xx + yy );

	return mat;
}

/*
=====================
idQuat::ToMat4
=====================
*/
idMat4 idQuat::ToMat4( void ){
	return ToMat3().ToMat4();
}

/*
=====================
idQuat::ToCQuat
=====================
*/
idCQuat idQuat::ToCQuat( void ){
	if ( w < 0.0f ) {
		return idCQuat( -x, -y, -z );
	}
	return idCQuat( x, y, z );
}

/*
============
idQuat::ToAngularVelocity
============
*/
vec3_t idQuat::ToAngularVelocity( void ){
	vec3_t vec;

	vec.x = x;
	vec.y = y;
	vec.z = z;
	vec.Normalize();
	return vec * acos( w );
}

///*
//=============
//idQuat::ToString
//=============
//*/
//const char *idQuat::ToString( int precision ){
//	return idStr::FloatArrayToString( ToFloatPtr(), GetDimension(), precision );
//}

/*
=====================
idQuat::Slerp

Spherical linear interpolation between two quaternions.
=====================
*/
idQuat &idQuat::Slerp( const idQuat &from, const idQuat &to, float t ) {
	idQuat	temp;
	float	omega, cosom, sinom, scale0, scale1;

	if ( t <= 0.0f ) {
		*this = from;
		return *this;
	}

	if ( t >= 1.0f ) {
		*this = to;
		return *this;
	}

	if ( from == to ) {
		*this = to;
		return *this;
	}

	cosom = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
	if ( cosom < 0.0f ) {
		temp = -to;
		cosom = -cosom;
	} else {
		temp = to;
	}

	if ( ( 1.0f - cosom ) > 1e-6f ) {

		scale0 = 1.0f - cosom * cosom;
		sinom = idMath::InvSqrt( scale0 );
		omega = idMath::ATan16( scale0 * sinom, cosom );
		scale0 = idMath::Sin16( ( 1.0f - t ) * omega ) * sinom;
		scale1 = idMath::Sin16( t * omega ) * sinom;
	} else {
		scale0 = 1.0f - t;
		scale1 = t;
	}

	*this = ( scale0 * from ) + ( scale1 * temp );
	return *this;
}

/*
=============
idCQuat::ToAngles
=============
*/
idAngles idCQuat::ToAngles( void ){
	return ToQuat().ToAngles();
}

/*
=============
idCQuat::ToRotation
=============
*/
idRotation idCQuat::ToRotation( void ){
	return ToQuat().ToRotation();
}

/*
=============
idCQuat::ToMat3
=============
*/
idMat3 idCQuat::ToMat3( void ){
	return ToQuat().ToMat3();
}

/*
=============
idCQuat::ToMat4
=============
*/
idMat4 idCQuat::ToMat4( void ){
	return ToQuat().ToMat4();
}

/*
=============
idCQuat::ToString
=============
*/
const char *idCQuat::ToString( int precision ){
	return idStr::FloatArrayToString( ToFloatPtr(), GetDimension(), precision );
}
