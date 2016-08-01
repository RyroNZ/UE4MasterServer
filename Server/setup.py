'''
                    RyroNZ's Master Server for UE4 v15.06.01
Master Server database designed to interface with RyroNZ's UE4 Networking Plugin
Copyright (c) 2016 Ryan Post
This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:
1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
'''

from setuptools import setup
setup(name='MasterServer',
      description="HTTP Master Server database designed to interface with RyroNZ's UE4 Client Plug-in",
      license="GNU General Public License 3",
      version='15.06.01',
      py_modules=['MasterServer'],
      install_requires = ["peewee",
                         "flask-peewee",
                         "mongoengine",
                         "requests",
                         "flask"]
      )
