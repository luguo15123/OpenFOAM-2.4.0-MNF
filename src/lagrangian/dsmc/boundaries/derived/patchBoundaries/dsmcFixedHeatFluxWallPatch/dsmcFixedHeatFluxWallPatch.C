/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 1991-2007 OpenCFD Ltd.
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

Description

The wall temperature is a function of the heat flux from incident parcels.

The radiation equation Q = epsilon*sigma*T^4 is used to calculate the wall temperature.

This class gives a single value of temperature for the whole patch.

\*---------------------------------------------------------------------------*/

#include "dsmcFixedHeatFluxWallPatch.H"
#include "addToRunTimeSelectionTable.H"
#include "fvc.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{

defineTypeNameAndDebug(dsmcFixedHeatFluxWallPatch, 0);

addToRunTimeSelectionTable(dsmcPatchBoundary, dsmcFixedHeatFluxWallPatch, dictionary);



// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

// Construct from components
dsmcFixedHeatFluxWallPatch::dsmcFixedHeatFluxWallPatch
(
    Time& t,
    const polyMesh& mesh,
    dsmcCloud& cloud,
    const dictionary& dict
)
:
    dsmcPatchBoundary(t, mesh, cloud, dict),
    propsDict_(dict.subDict(typeName + "Properties")),
    wallTemperature_
    (
        IOobject
        (
            "wallTemperature",
            time_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("0.0", dimTemperature, 0.0)
    ),
    EcTot_(0.0),
    EcTotSum_(0.0),
    newWallTemperature_(0.0),
    desiredHeatFlux_(),
    relaxationFactor_(),
    stepCounter_(0),
    nSamples_(0)
{
    writeInTimeDir_ = false;
    writeInCase_ = false;
    measurePropertiesAtWall_ = true;

    setProperties();

    newWallTemperature_ = temperature_;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

dsmcFixedHeatFluxWallPatch::~dsmcFixedHeatFluxWallPatch()
{}



// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //
void dsmcFixedHeatFluxWallPatch::initialConfiguration()
{}


void dsmcFixedHeatFluxWallPatch::calculateProperties()
{
    stepCounter_++;

    const scalar deltaT = mesh_.time().deltaTValue();
    
    EcTotSum_ += EcTot_;
    EcTot_ = 0.0;

    
    if(time_.time().outputTime())
    {
        scalar fA = totalPatchSurfaceArea_;

        scalar heatFlux = EcTotSum_/(deltaT*stepCounter_*fA);
                   
        if(fabs(heatFlux) > VSMALL) // zero face temperature not allowed!
        {
            Pout << "heatFlux = " << heatFlux << endl;
            
            scalar oldWallTemperature = newWallTemperature_;
            
            newWallTemperature_ = oldWallTemperature
                *(1.0 + (relaxationFactor_/1000.0)*((heatFlux - desiredHeatFlux_)/(fabs(desiredHeatFlux_) + 100.0)));
                
            scalar newTemp = newWallTemperature_;
                
            if(newTemp - oldWallTemperature > 100.0)
            {
                newWallTemperature_ = oldWallTemperature + 50.0;
            }
            
            if(newTemp - oldWallTemperature < -100.0)
            {
                newWallTemperature_ = oldWallTemperature - 50.0;
            }
                
            if(newWallTemperature_ < VSMALL)
            {
                newWallTemperature_ = temperature_;
            }
            
            Pout << "newWallTemperature_ = " << newWallTemperature_ << endl;
        }                                    
        
        label wppIndex = patchId_;
        
        if(newWallTemperature_ > VSMALL)
        {
            wallTemperature_.boundaryField()[wppIndex] = newWallTemperature_;
        }
        
        if( mag(heatFlux - desiredHeatFlux_) > 1.00e5 )
        {
            stepCounter_ = 0.0;
            EcTotSum_ = 0.0;
        }
    }
}

void dsmcFixedHeatFluxWallPatch::controlParticle(dsmcParcel& p, dsmcParcel::trackingData& td)
{
    measurePropertiesBeforeControl(p);

    vector& U = p.U();

    scalar& ERot = p.ERot();
    
    label& vibLevel = p.vibLevel();

    label typeId = p.typeId();

    scalar m = cloud_.constProps(typeId).mass();

    scalar preIE = 0.5*m*(U & U) + ERot + vibLevel*physicoChemical::k.value()*cloud_.constProps(typeId).thetaV();

    vector nw = p.normal();
    nw /= mag(nw);

    // Normal velocity magnitude
    scalar U_dot_nw = U & nw;

    // Wall tangential velocity (flow direction)
    vector Ut = U - U_dot_nw*nw;

    Random& rndGen(cloud_.rndGen());

    while (mag(Ut) < SMALL)
    {
        // If the incident velocity is parallel to the face normal, no
        // tangential direction can be chosen.  Add a perturbation to the
        // incoming velocity and recalculate.

        U = vector
        (
            U.x()*(0.8 + 0.2*rndGen.scalar01()),
            U.y()*(0.8 + 0.2*rndGen.scalar01()),
            U.z()*(0.8 + 0.2*rndGen.scalar01())
        );

        U_dot_nw = U & nw;

        Ut = U - U_dot_nw*nw;
    }

    // Wall tangential unit vector
    vector tw1 = Ut/mag(Ut);

    // Other tangential unit vector
    vector tw2 = nw^tw1;

    const scalar& T = newWallTemperature_;

    scalar mass = cloud_.constProps(typeId).mass();

    scalar rotationalDof = cloud_.constProps(typeId).rotationalDegreesOfFreedom();
    
    scalar vibrationalDof = cloud_.constProps(typeId).vibrationalDegreesOfFreedom();

    U =
        sqrt(physicoChemical::k.value()*T/mass)
       *(
            rndGen.GaussNormal()*tw1
          + rndGen.GaussNormal()*tw2
          - sqrt(-2.0*log(max(1 - rndGen.scalar01(), VSMALL)))*nw
        );

    ERot = cloud_.equipartitionRotationalEnergy(T, rotationalDof);
    
    vibLevel = cloud_.equipartitionVibrationalEnergyLevel(T, vibrationalDof, typeId);

    measurePropertiesAfterControl(p, 0.0);
    
    scalar postIE = 0.5*m*(U & U) + ERot + vibLevel*physicoChemical::k.value()*cloud_.constProps(typeId).thetaV();
    
    U += velocity_;
    
    EcTot_ += cloud_.nParticle()*(preIE - postIE);
    
    nSamples_++;
}

void dsmcFixedHeatFluxWallPatch::output
(
    const fileName& fixedPathName,
    const fileName& timePath
)
{

}

void dsmcFixedHeatFluxWallPatch::updateProperties(const dictionary& newDict)
{
    //- the main properties should be updated first
    updateBoundaryProperties(newDict);

    propsDict_ = newDict.subDict(typeName + "Properties");

    setProperties();
}

void dsmcFixedHeatFluxWallPatch::setProperties()
{
    velocity_ = propsDict_.lookup("velocity");
    temperature_ = readScalar(propsDict_.lookup("initialTemperature"));
    desiredHeatFlux_ = readScalar(propsDict_.lookup("desiredHeatFlux"));
    relaxationFactor_ = readScalar(propsDict_.lookup("relaxationFactor"));
}

} // End namespace Foam

// ************************************************************************* //
