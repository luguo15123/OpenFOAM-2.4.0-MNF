/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2009-2010 OpenCFD Ltd.
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "dsmcParcel.H"
#include "IOstreams.H"
#include "IOField.H"
#include "dsmcCloud.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

//template <class ParcelType>
Foam::dsmcParcel::dsmcParcel
(
    const polyMesh& mesh,
    Istream& is,
    bool readFields
)
:
    particle(mesh, is, readFields),
    U_(vector::zero),
    ERot_(0.0),
    vibLevel_(0),
    ELevel_(0),
    typeId_(-1),
    newParcel_(0),
    classification_(0)
{
    if (readFields)
    {
        if (is.format() == IOstream::ASCII)
        {
            is >> U_;
            ERot_ = readScalar(is);
            vibLevel_ = readLabel(is);
            ELevel_ = readLabel(is);
            typeId_ = readLabel(is);
            newParcel_ = readLabel(is);
            classification_ = readLabel(is);
        }
        else
        {
            is.read
            (
                reinterpret_cast<char*>(&U_),
                sizeof(U_)
                + sizeof(ERot_)
                + sizeof(vibLevel_)
                + sizeof(ELevel_)
                + sizeof(typeId_)
                + sizeof(newParcel_)
                + sizeof(classification_)
            );
        }
    }

    // Check state of Istream
    is.check
    (
        "Foam::dsmcParcel::dsmcParcel"
        "(const Cloud<dsmcParcel>& cloud, Foam::Istream&), bool"
    );
}


//template <class ParcelType>
void Foam::dsmcParcel::readFields(Cloud<dsmcParcel>& c)
{
    if (!c.size())
    {
        return;
    }

    particle::readFields(c);

    IOField<vector> U(c.fieldIOobject("U", IOobject::MUST_READ));
    c.checkFieldIOobject(c, U);

    IOField<scalar> ERot(c.fieldIOobject("ERot", IOobject::MUST_READ));
    c.checkFieldIOobject(c, ERot);
    
    IOField<label> vibLevel(c.fieldIOobject("vibLevel", IOobject::MUST_READ));
    c.checkFieldIOobject(c, vibLevel);
    
    IOField<label> ELevel(c.fieldIOobject("ELevel", IOobject::MUST_READ));
    c.checkFieldIOobject(c, ELevel);

    IOField<label> typeId(c.fieldIOobject("typeId", IOobject::MUST_READ));
    c.checkFieldIOobject(c, typeId);

    IOField<label> newParcel(c.fieldIOobject("newParcel", IOobject::MUST_READ));
    c.checkFieldIOobject(c, newParcel);
    
    IOField<label> classification(c.fieldIOobject("classification", IOobject::MUST_READ));
    c.checkFieldIOobject(c, classification);

    label i = 0;
    forAllIter(dsmcCloud, c, iter)
    {
        dsmcParcel& p = iter();

        p.U_ = U[i];
        p.ERot_ = ERot[i];
        p.vibLevel_ = vibLevel[i];
        p.ELevel_ = ELevel[i];
        p.typeId_ = typeId[i];
        p.newParcel_ = newParcel[i];
        p.classification_ = classification[i];
        i++;
    }
}


//template <class ParcelType>
void Foam::dsmcParcel::writeFields(const Cloud<dsmcParcel>& c)
{
    particle::writeFields(c);

    label np =  c.size();

    IOField<vector> U(c.fieldIOobject("U", IOobject::NO_READ), np);
    IOField<scalar> ERot(c.fieldIOobject("ERot", IOobject::NO_READ), np);
    IOField<label> vibLevel(c.fieldIOobject("vibLevel", IOobject::NO_READ), np);
    IOField<label> ELevel(c.fieldIOobject("ELevel", IOobject::NO_READ), np);
    IOField<label> typeId(c.fieldIOobject("typeId", IOobject::NO_READ), np);
    IOField<label> newParcel(c.fieldIOobject("newParcel", IOobject::NO_READ), np);
    IOField<label> classification(c.fieldIOobject("classification", IOobject::NO_READ), np);

    label i = 0;
    forAllConstIter(dsmcCloud, c, iter)
    {
        const dsmcParcel& p = iter();

        U[i] = p.U();
        ERot[i] = p.ERot();
        vibLevel[i] = p.vibLevel();
        ELevel[i] = p.ELevel();
        typeId[i] = p.typeId();
        newParcel[i] = p.newParcel();
        classification[i] = p.classification();
        i++;
    }

    U.write();
    ERot.write();
    vibLevel.write();
    ELevel.write();
    typeId.write();
    newParcel.write();
    classification.write();
}


// * * * * * * * * * * * * * * * IOstream Operators  * * * * * * * * * * * * //

//template <class ParcelType>
Foam::Ostream& Foam::operator<<
(
    Ostream& os,
    const dsmcParcel& p
)
{
    if (os.format() == IOstream::ASCII)
    {
        os  << static_cast<const particle&>(p) 
            << token::SPACE << p.U()
            << token::SPACE << p.ERot()
            << token::SPACE << p.vibLevel()
            << token::SPACE << p.ELevel()
            << token::SPACE << p.typeId()
            << token::SPACE << p.newParcel()
            << token::SPACE << p.classification();
    }
    else
    {
        os  << static_cast<const particle&>(p);
        os.write
        (
            reinterpret_cast<const char*>(&p.U_),
            sizeof(p.U())
            + sizeof(p.ERot())
            + sizeof(p.vibLevel())
            + sizeof(p.ELevel())
            + sizeof(p.typeId())
            + sizeof(p.newParcel())
            + sizeof(p.classification())
        );
    }

    // Check state of Ostream
    os.check
    (
        "Foam::Ostream& Foam::operator<<"
        "(Foam::Ostream&, const Foam::dsmcParcel&)"
    );

    return os;
}


// ************************************************************************* //
