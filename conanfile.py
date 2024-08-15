from conan import ConanFile

from conan.tools.files import copy
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout


class App(ConanFile):

    settings = "os", "compiler", "build_type", "arch"

    options = {}

    def requirements(self):
        if self.settings.os == "Linux":
            self.requires("linux-headers-generic/6.5.9")

    def build_requirements(self):
        self.tool_requires("bison/3.8.2")
        self.tool_requires("m4/1.4.19")
        self.tool_requires("ragel/6.10")
        self.tool_requires("yasm/1.3.0")

    def generate(self):
        CMakeDeps(self).generate()
        CMakeToolchain(self).generate()

        for dep in self.dependencies.values():
            if dep.cpp_info.bindirs:
                copy(self, pattern="*yasm*", src=dep.cpp_info.bindirs[0], dst=self.build_folder + "../../../.././bin")
                copy(self, pattern="bison*", src=dep.cpp_info.bindirs[0], dst=self.build_folder + "../../../.././bin/bison/bin")
                copy(self, pattern="m4*", src=dep.cpp_info.bindirs[0], dst=self.build_folder + "../../../.././bin/m4/bin")
                copy(self, pattern="ragel*", src=dep.cpp_info.bindirs[0], dst=self.build_folder + "../../../.././bin")
                copy(self, pattern="ytasm*", src=dep.cpp_info.bindirs[0], dst=self.build_folder + "../../../.././bin")
                copy(self, pattern="*", src=dep.cpp_info.bindirs[0], dst=self.build_folder + "../../../.././bin/bison/res")
    def layout(self):
        cmake_layout(self)
