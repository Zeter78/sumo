/****************************************************************************/
// Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
// Copyright (C) 2001-2020 German Aerospace Center (DLR) and others.
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// https://www.eclipse.org/legal/epl-2.0/
// This Source Code may also be made available under the following Secondary
// Licenses when the conditions for such availability set forth in the Eclipse
// Public License 2.0 are satisfied: GNU General Public License, version 2
// or later which is available at
// https://www.gnu.org/licenses/old-licenses/gpl-2.0-standalone.html
// SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later
/****************************************************************************/
/// @file    ValueSource.h
/// @author  Daniel Krajzewicz
/// @date    Fri, 29.04.2005
///
//
/****************************************************************************/
#ifndef ValueSource_h
#define ValueSource_h


// ===========================================================================
// included modules
// ===========================================================================


// ===========================================================================
// class definitions
// ===========================================================================
/**
 *
 */
template<typename T>
class ValueSource {
public:
    ValueSource() { }
    virtual ~ValueSource() { }
    virtual T getValue() const = 0;
    virtual ValueSource* copy() const = 0;
    virtual ValueSource<double>* makedoubleReturningCopy() const = 0;

};


#endif

/****************************************************************************/

