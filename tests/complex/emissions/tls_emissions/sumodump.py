# Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
# Copyright (C) 2012-2020 German Aerospace Center (DLR) and others.
# This program and the accompanying materials are made available under the
# terms of the Eclipse Public License 2.0 which is available at
# https://www.eclipse.org/legal/epl-2.0/
# This Source Code may also be made available under the following Secondary
# Licenses when the conditions for such availability set forth in the Eclipse
# Public License 2.0 are satisfied: GNU General Public License, version 2
# or later which is available at
# https://www.gnu.org/licenses/old-licenses/gpl-2.0-standalone.html
# SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later

# @file    sumodump.py
# @author  Daniel Krajzewicz
# @date    2013-10-19


from __future__ import absolute_import
from xml.sax import handler, make_parser


class DumpReader(handler.ContentHandler):

    def __init__(self, toCollect):
        self._values = {}
        self._toCollect = toCollect
        self._intervalBegins = []
        self._beginTime = None
        for a in self._toCollect:
            self._values[a] = []

    def startElement(self, name, attrs):
        if name == 'interval':
            self._beginTime = float(attrs['begin'])
            self._intervalBegins.append(self._beginTime)
            for a in self._toCollect:
                self._values[a].append({})
        if name == 'edge' or name == 'lane':
            id = attrs['id']
            for a in attrs.keys():
                if a not in self._toCollect:
                    continue
                self._values[a][-1][id] = float(attrs[a])

    def join(self, what, how):
        for a in what:
            self._singleJoin(a, how)

    def get(self, what):
        return self._values[what]

    def _singleJoin(self, what, how):
        ret = {}
        no = {}
        for i in self._values[what]:
            for e in i:
                if e not in ret:
                    ret[e] = 0
                    no[e] = 0
                ret[e] = ret[e] + i[e]
                no[e] = no[e] + 1
        if how == "sum":
            return ret
        elif how == "average":
            for e in i:
                ret[e] = ret[e] / float(no[e])
        self._values[what] = [ret]


def readDump(file, toCollect):
    parser = make_parser()
    dump = DumpReader(toCollect)
    parser.setContentHandler(dump)
    parser.parse(file)
    return dump
