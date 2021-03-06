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

\*---------------------------------------------------------------------------*/

#include "recombination.H"
#include "addToRunTimeSelectionTable.H"
#include "fvc.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{

defineTypeNameAndDebug(recombination, 0);

addToRunTimeSelectionTable(dsmcReaction, recombination, dictionary);



// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

// Construct from components
recombination::recombination
(
    Time& t,
    dsmcCloud& cloud,
    const dictionary& dict
)
:
    dsmcReaction(t, cloud, dict),
    propsDict_(dict.subDict(typeName + "Properties")),
    reactantIds_(),
    productId_(),
    thirdBodyId_(),
    aCoeff_(readScalar(propsDict_.lookup("aCoeff"))),
    bCoeff_(readScalar(propsDict_.lookup("bCoeff"))),
    reactionName_(propsDict_.lookup("reactionName")),
    heatOfReactionRecombination_(readScalar(propsDict_.lookup("heatOfReactionRecombination"))),
    relax_(true),
    allowSplitting_(false),
    writeRatesToTerminal_(false),
    cellVolume_(0.0),
    rhoA_(0.0),
    rhoB_(0.0),
    rhoC_(0.0)
{
    forAll(mesh_.cells(), c)
    {
        cellVolume_ += mesh_.cellVolumes()[c];
    }
    
    if(Pstream::parRun())
    {
        reduce(cellVolume_, sumOp<scalar>());
    }

}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

recombination::~recombination()
{}

// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void recombination::initialConfiguration()
{
    setProperties();
    computeDensity();
}
    
    
void recombination::computeDensity()
{    
    const List< DynamicList<dsmcParcel*> >& cellOccupancy
        = cloud_.cellOccupancy();

    label molsA = 0;
    label molsB = 0;
    label molsC = 0;

    forAll(cellOccupancy, c)
    {
        const List<dsmcParcel*>& parcelsInCell = cellOccupancy[c];
    
        forAll(parcelsInCell, pIC)
        {
            dsmcParcel* p = parcelsInCell[pIC];
            
            if(p->typeId() == reactantIds_[0])
            {
                molsA++;
            }
                
            if(p->typeId() == reactantIds_[1])
            {
                molsB++;
            }

            if(p->typeId() == thirdBodyId_)
            {
                molsC++;
            }
        }
    }

    //- Parallel communication
    if(Pstream::parRun())
    {
        reduce(molsA, sumOp<label>());
        reduce(molsB, sumOp<label>());
        reduce(molsC, sumOp<label>());
    }

    rhoA_ = (molsA*cloud().nParticle())/cellVolume_;
    rhoB_ = (molsB*cloud().nParticle())/cellVolume_;
    rhoC_ = (molsC*cloud().nParticle())/cellVolume_;
}

void recombination::setProperties()
{
    // reading in reactants

    const List<word> reactantMolecules (propsDict_.lookup("reactantAtomsToRecombine"));

    if(reactantMolecules.size() != 2)
    {
        FatalErrorIn("recombination::setProperties()")
            << "There should be two reactants atoms, instead of " 
            << reactantMolecules.size() << nl 
            << exit(FatalError);
    }
    
    if(reactantMolecules[0] != reactantMolecules[1])
    {
        FatalErrorIn("recombination::setProperties()")
            << "Both reactant species must be the same, they are currently " 
	    << reactantMolecules[0] << " and " << reactantMolecules[1] << nl 
            << exit(FatalError);
    }

    reactantIds_.setSize(reactantMolecules.size(), -1);

    allowSplitting_ = Switch(propsDict_.lookup("allowSplitting"));
    
    writeRatesToTerminal_ = Switch(propsDict_.lookup("writeRatesToTerminal"));

    forAll(reactantIds_, r)
    {
        reactantIds_[r] = findIndex(cloud_.typeIdList(), reactantMolecules[r]);

        // check that reactants belong to the typeIdList (constant/dsmcProperties)
        if(reactantIds_[r] == -1)
        {
            FatalErrorIn("recombination::setProperties()")
                << "Cannot find type id: " << reactantMolecules[r] << nl 
                << exit(FatalError);
        }

        // check that reactants are 'ATOMS' (not 'MOLECULES') 

        const scalar& rDofReactant = cloud_.constProps(reactantIds_[r]).rotationalDegreesOfFreedom();
    
        if(rDofReactant > VSMALL)
        {
            FatalErrorIn("recombination::setProperties()")
                << "Reactant must be an atom (not a molecule): " << reactantMolecules[r] 
                << nl 
                << exit(FatalError);
        }
    }

    // reading in product

    const word productMolecule (propsDict_.lookup("productOfRecombination"));
    
    productId_ = findIndex(cloud_.typeIdList(), productMolecule);

    // check that reactants belong to the typeIdList (constant/dsmcProperties)
    if(productId_ == -1)
    {
        FatalErrorIn("recombination::setProperties()")
            << "Cannot find type id: " << productMolecule << nl 
            << exit(FatalError);
    }

    // check that products are 'MOLECULES' (not 'ATOMS') 

    const scalar& rDofThirdBody = cloud_.constProps(productId_).rotationalDegreesOfFreedom();

    if(rDofThirdBody < 1)
    {
        FatalErrorIn("recombination::setProperties()")
            << "Reactant must be a molecule (not an atom): " << productMolecule 
            << nl 
            << exit(FatalError);
    }
    
    //reading in third body molecule
    
    const word thirdBodyMolecule (propsDict_.lookup("thirdBodyMolecule"));
    
    thirdBodyId_ = findIndex(cloud_.typeIdList(), thirdBodyMolecule);

    // check that reactants belong to the typeIdList (constant/dsmcProperties)
    if(thirdBodyId_ == -1)
    {
        FatalErrorIn("recombination::setProperties()")
            << "Cannot find type id: " << thirdBodyMolecule << nl 
            << exit(FatalError);
    }

    // check that products are 'MOLECULES' (not 'ATOMS') 

    const scalar& rDof = cloud_.constProps(thirdBodyId_).rotationalDegreesOfFreedom();

    if(rDof < 1)
    {
        FatalErrorIn("recombination::setProperties()")
            << "Third body must be a molecule (not an atom): " << thirdBodyMolecule 
            << nl 
            << exit(FatalError);
    }

}

bool recombination::tryReactMolecules(const label& typeIdP, const label& typeIdQ)
{
    label reactantPId = findIndex(reactantIds_, typeIdP);
    label reactantQId = findIndex(reactantIds_, typeIdQ);

    if(reactantPId == reactantQId)
    {
        if
        (
            (reactantPId != -1) &&
            (reactantQId != -1) 
        )
        {
            return true;
        }
    }

    if
    (
        (reactantPId != -1) &&
        (reactantQId != -1) &&
        (reactantPId != reactantQId)
    )
    {
        return true;
    }
    else
    {
        return false;
    }
}

void recombination::reaction
(
    dsmcParcel& p,
    dsmcParcel& q,
    const DynamicList<label>& candidateList,
    const List<DynamicList<label> >& candidateSubList,
    const label& candidateP,
    const List<label>& whichSubCell
)
{
}


void recombination::reaction
(
    dsmcParcel& p,
    dsmcParcel& q
)
{
    label typeIdP = p.typeId();
    label typeIdQ = q.typeId();
    
    if(typeIdP == typeIdQ && typeIdP == reactantIds_[0]) // same species and desired species to recombine
    {
        vector UP = p.U();
        vector UQ = q.U();
        scalar mP = cloud_.constProps(typeIdP).mass();
        scalar mQ = cloud_.constProps(typeIdQ).mass();
        scalar omegaP = cloud_.constProps(typeIdP).omega();
        scalar omegaQ = cloud_.constProps(typeIdQ).omega();
        
        scalar dP = cloud_.constProps(typeIdP).d();
        scalar dQ = cloud_.constProps(typeIdQ).d();
        scalar dThirdBody = cloud_.constProps(thirdBodyId_).d();
        
        scalar VRef = (pi/6.0)*(pow((dP+dQ+dThirdBody),3.0));
        
        scalar omegaPQ = 0.5*(omegaP + omegaQ);

        scalar mR = mP*mQ/(mP + mQ);

        scalar cRsqr = magSqr(UP - UQ);
        
        scalar translationalEnergy = 0.5*mR*cRsqr;
        
        scalar TColl = ((mR*cRsqr)/(2.0*physicoChemical::k.value()))/(2.5 - omegaPQ);
        
        scalar aPrime = aCoeff_*(pow((2.5 - omegaPQ),bCoeff_)*exp(Foam::lgamma(2.5 - omegaPQ))/exp(Foam::lgamma((2.5 - omegaPQ + bCoeff_))));
        
        scalar VColl = aPrime*pow((TColl/cloud_.constProps(thirdBodyId_).thetaV()),bCoeff_)*VRef;
		
// 		scalar VColl = aCoeff_*pow((4000.00/cloud_.constProps(thirdBodyId_).thetaV()),bCoeff_)*VRef;
        
        scalar pRec = rhoC_*VColl;
        
        if(pRec > cloud_.rndGen().scalar01())
        {
            nReactionsPerTimeStep_++;
            nTotReactions_++;
            
            relax_ = false;

            if (allowSplitting_)
            {
                // RECOMBINATION REACTION //

                //center of mass velocity (pre-recombination)
                vector Ucm = (mP*UP + mQ*UQ)/(mP + mQ);

                const label& typeIdRecombinedMol = productId_;

                // change species properties

                scalar mP = cloud_.constProps(typeIdP).mass();

                scalar mQ = cloud_.constProps(typeIdQ).mass();

                scalar thetaVP = cloud_.constProps(typeIdRecombinedMol).thetaV();

                scalar mR = mP*mQ/(mP + mQ);

                scalar omegaPQ =
                0.5
                *(
                    cloud_.constProps(typeIdP).omega()
                    + cloud_.constProps(typeIdQ).omega()
                );

                ///////////////////////////////////////////////////////////////////////////////////
                // Determine the pre-collision total energy. Note that for this exothermic       //
                // recombination reaction the heatOfReactionJoules is added to the total.        //
                // heatOfReactionJoules is positive in the chemReactDict.                        //
                ///////////////////////////////////////////////////////////////////////////////////

                scalar heatOfReactionJoules = heatOfReactionRecombination_*physicoChemical::k.value();

                scalar EcTot = translationalEnergy + heatOfReactionJoules;

                scalar func = 0.0;
                scalar rel = 0.0;
                scalar psiV = 0.0;

                ///////////////////////////////////////
                // acceptance-rejection method (ARM) //
                ///////////////////////////////////////

                scalar DOFrot = cloud_.constProps(typeIdRecombinedMol).rotationalDegreesOfFreedom();

                scalar DOFm = 5.0 - (2.0*omegaPQ);
                scalar DOFsum = DOFm + DOFrot;

                scalar expo = 0.5*DOFsum -1.0;

                rel = EcTot / (physicoChemical::k.value()*thetaVP);
                
                do
                {
                    psiV = label(((label(rel)) +1)*cloud_.rndGen().scalar01())/rel;
                    func = pow((1.0-psiV),expo);
                } while (func < cloud_.rndGen().scalar01());

                scalar Evib = psiV* EcTot;

                EcTot -= Evib;


                // Distribute EcTot over translational and all rotational modes of recombined molecule based on MONACO code //

                DOFm = 1.0*(5.0 - (2.0 * omegaPQ)); // part of MONACO code

                scalar DOFtot = DOFm + DOFrot;

                scalar rPSIm = 0.0;

                scalar h1 = 0.5*DOFtot - 2.0;
                scalar h2 = 0.5*DOFm - 1.0 + SMALL;
                scalar h3 = 0.5*(DOFtot-DOFm)-1.0 + SMALL;

                scalar prob = 0.0;

                do
                {
                    rPSIm = cloud_.rndGen().scalar01();
                    prob = pow(h1,h1)/(pow(h2,h2)*pow(h3,h3))*pow(rPSIm,h2)*pow(1.0-rPSIm,h3);

                } while (prob < cloud_.rndGen().scalar01());

                if (DOFm == DOFtot) rPSIm = 1.0;

                if (DOFm == 2.0 && DOFtot == 4.0) rPSIm = cloud_.rndGen().scalar01();

                if (DOFtot < 4.0 ) rPSIm = (DOFm/DOFtot);

                scalar Erel = EcTot*rPSIm;
                scalar Erot = EcTot - Erel;

                scalar relVelRecombMol = pow(2*Erel/mR,0.5);

                // Variable Hard Sphere collision part for collision of molecules
        
                scalar cosTheta = 2.0*cloud_.rndGen().scalar01() - 1.0;
            
                scalar sinTheta = sqrt(1.0 - cosTheta*cosTheta);
            
                scalar phi = twoPi*cloud_.rndGen().scalar01();
            
                vector postCollisionRelU =
                    relVelRecombMol
                    *vector
                        (
                            cosTheta,
                            sinTheta*cos(phi),
                            sinTheta*sin(phi)
                        );
        
                UP = Ucm + (postCollisionRelU*mQ/(mP + mQ));
                UQ = Ucm - (postCollisionRelU*mP/(mP + mQ));
                
                vector URecombined = 0.5*(UP + UQ);
                
                label cellI = p.cell();
                vector position = p.position();
                label tetFaceI = p.tetFace();
                label tetPtI = p.tetPt();
                
                label classification = 0;
                
                if(cloud_.rndGen().scalar01() > 0.5)
                {
                    classification = p.classification();
                }
                else
                {
                    classification = q.classification();
                }
                
                cloud_.deleteParticle(p);
                cloud_.deleteParticle(q);
                
                cloud_.addNewParcel
                (
                    position,
                    URecombined,
                    Erot,
                    Evib,
                    0.0,
                    cellI,
                    tetFaceI,
                    tetPtI,
                    productId_,
                    0,
                    classification
                );
            }
            else
            {
                relax_ = true; // no reaction - use LB
            }
        }
    }
}

void  recombination::outputResults(const label& counterIndex)
{
     if(writeRatesToTerminal_ == true)
     {
        // measure density 
        computeDensity();

        Info << "species A number density: " << rhoA_ << endl;
        Info << "species B number density: " << rhoB_ << endl;
        Info << "species C number density: " << rhoC_ << endl;

        const scalar& deltaT = mesh_.time().deltaT().value();

        const List<word> reactantMolecules (propsDict_.lookup("reactantAtomsToRecombine"));

        const word productMolecule (propsDict_.lookup("productOfRecombination"));

        const word thirdBodyMolecule (propsDict_.lookup("thirdBodyMolecule"));

        scalar reactionRate =
        (
            nTotReactions()
            * cloud_.nParticle()
        )/(counterIndex*deltaT*rhoA_*rhoB_*rhoC_*cellVolume_);

        Info << "Number of reactions per timestep = " << nReactionsPerTimeStep_ << endl;

        Info << "Recombination reaction " <<  reactantMolecules[0] << " + " << reactantMolecules[1] << " + " << thirdBodyMolecule << 
                " --> " << productMolecule << " + " << thirdBodyMolecule << 
                ", total number of reactions = " << nTotReactions() << ", reaction rate = " << reactionRate << endl;
     }

    nReactionsPerTimeStep_ = 0.0;
}


const bool& recombination::relax() const
{
    return relax_;
}

}
// End namespace Foam

// ************************************************************************* //
