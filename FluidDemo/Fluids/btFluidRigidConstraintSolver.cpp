/*
Bullet-FLUIDS 
Copyright (c) 2012 Jackson Lee

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. 
   If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/
#include "btFluidRigidConstraintSolver.h"

#include "LinearMath/btVector3.h"

#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "btFluidSph.h"


void btFluidRigidConstraintSolver::resolveCollisionsSingleFluid(const btFluidParametersGlobal& FG, btFluidSph *fluid)
{
	BT_PROFILE("resolveCollisionsSingleFluid()");

	const btAlignedObjectArray<btFluidRigidContactGroup>& contactGroups = fluid->internalGetRigidContacts();

	btAlignedObjectArray<btVector3> accumulatedForces;	//Each element corresponds to a btCollisionObject
	btAlignedObjectArray<btVector3> accumulatedTorques;
	
	accumulatedForces.resize( contactGroups.size() );
	accumulatedTorques.resize( contactGroups.size() );
	
	for(int i = 0; i < accumulatedForces.size(); ++i) accumulatedForces[i].setValue(0,0,0);
	for(int i = 0; i < accumulatedTorques.size(); ++i) accumulatedTorques[i].setValue(0,0,0);
	
	//Accumulate forces on rigid bodies
	for(int i = 0; i < contactGroups.size(); ++i)
	{
		const btFluidRigidContactGroup& current = contactGroups[i];
	
		for(int n = 0; n < current.numContacts(); ++n)
		{
			resolveCollisionPenaltyForce(FG, fluid, const_cast<btCollisionObject*>(current.m_object),
										 current.m_contacts[n], accumulatedForces[i], accumulatedTorques[i]);
		}
	}
	
	//Apply forces to rigid bodies
	const btScalar timeStep = FG.m_timeStep;
	for(int i = 0; i < contactGroups.size(); ++i)
	{
		btRigidBody* rigidBody = btRigidBody::upcast( const_cast<btCollisionObject*>(contactGroups[i].m_object) );
		if( rigidBody && rigidBody->getInvMass() != btScalar(0.0) )
		{
			rigidBody->activate(true);
			
			btVector3 linearVelocity = rigidBody->getLinearVelocity();
			btVector3 angularVelocity = rigidBody->getAngularVelocity();
			
			linearVelocity += accumulatedForces[i] * (rigidBody->getInvMass() * timeStep);
			angularVelocity += rigidBody->getInvInertiaTensorWorld() * accumulatedTorques[i] * timeStep;
			
			const btScalar MAX_ANGVEL = SIMD_HALF_PI;
			btScalar angVel = angularVelocity.length();
			if(angVel*timeStep > MAX_ANGVEL) angularVelocity *= (MAX_ANGVEL/timeStep) / angVel;
			
			rigidBody->setLinearVelocity(linearVelocity);
			rigidBody->setAngularVelocity(angularVelocity);
		}
	}
}

void btFluidRigidConstraintSolver::resolveCollisionPenaltyForce(const btFluidParametersGlobal& FG, btFluidSph* fluid, 
																btCollisionObject *object, const btFluidRigidContact& contact,
																btVector3 &accumulatedRigidForce, btVector3 &accumulatedRigidTorque)
{
	const btFluidParametersLocal& FL = fluid->getLocalParameters();
	
	if( contact.m_distance < btScalar(0.0) )
	{
		btRigidBody* rigidBody = btRigidBody::upcast(object);
		bool isDynamicRigidBody = ( rigidBody && rigidBody->getInvMass() != btScalar(0.0) );
		
		btVector3 rigidLocalHitPoint = contact.m_hitPointWorldOnObject - object->getWorldTransform().getOrigin();
		
		btVector3 fluidVelocity = fluid->getEvalVelocity(contact.m_fluidParticleIndex);
		btVector3 rigidVelocity = (isDynamicRigidBody) ? rigidBody->getVelocityInLocalPoint(rigidLocalHitPoint) : btVector3(0,0,0); 
		rigidVelocity *= FG.m_simulationScale;
	
		btVector3 relativeVelocity = fluidVelocity - rigidVelocity;
		btScalar relativeNormalMagnitude = contact.m_normalOnObject.dot(relativeVelocity);
		
		btScalar penetrationDepth = -contact.m_distance * FG.m_simulationScale;
		btScalar forceMagnitude = FL.m_boundaryStiff * penetrationDepth - FL.m_boundaryDamp * relativeNormalMagnitude;
		
		btVector3 acceleration = contact.m_normalOnObject * forceMagnitude;
		
		if( FL.m_boundaryFriction != btScalar(0.0) )
		{
			//Since an acceleration is used to resolve the collision,
			//the change in velocity per frame is (acceleration * FG.timeStep).
			const btScalar tangentRemovedPerFrame = FL.m_boundaryFriction / FG.m_timeStep; 
			
			btVector3 relativeNormalVelocity = contact.m_normalOnObject * relativeNormalMagnitude;
			btVector3 relativeTangentialVelocity = relativeVelocity - relativeNormalVelocity;
			acceleration -= relativeTangentialVelocity * tangentRemovedPerFrame;
		}
		
		btVector3 force = acceleration * FL.m_particleMass;
		
		if(isDynamicRigidBody)
		{
			btVector3 worldScaleForce = -force / FG.m_simulationScale;
		
			const btVector3& linearFactor = rigidBody->getLinearFactor();
			accumulatedRigidForce += worldScaleForce * linearFactor;
			accumulatedRigidTorque += rigidLocalHitPoint.cross(worldScaleForce * linearFactor) * rigidBody->getAngularFactor();
			
			rigidBody->activate(true);
		}
		
		//if acceleration is very high, the fluid simulation will explode
		fluid->applyForce(contact.m_fluidParticleIndex, force);
	}
}
