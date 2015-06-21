'''
                    RyroNZ's Master Server for UE4 v15.06.01

Master Server database designed to interface with RyroNZ's UE4 Networking Plugin
Copyright (C) 2015  Ryan Post

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

'''

from setuptools import setup
setup(name='MasterServer',
      description="Master Server database designed to interface with RyroNZ's UE4 Networking Plugin",
      license="GNU General Public License 3"
      version='15.06.01',
      py_modules=['MasterServer'],
      intall_requires = ["peewee",
                         "flask-peewee",
                         "mongoengine",
                         "requests",
                         "flask"]
      )
