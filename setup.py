__author__ = 'Ryan'

from distutils.core import setup
setup(name='MasterServer',
      version='1.0',
      py_modules=['MasterServer'],
      intall_requires = ["peewee", "flask-peewee", "mongoengine", "requests"]
      )