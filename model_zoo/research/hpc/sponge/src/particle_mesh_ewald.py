# Copyright 2021 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================
'''PME'''
import math


class Particle_Mesh_Ewald():
    '''PME'''
    def __init__(self, controller, md_info):
        self.module_name = "PME"
        self.CONSTANT_Pi = 3.1415926535897932
        self.cutoff = 10.0 if "cutoff" not in controller.Command_Set else float(controller.Command_Set["cutoff"])
        self.tolerance = 0.00001 if "Direct_Tolerance" not in controller.Command_Set else float(
            controller.Command_Set["Direct_Tolerance"])
        self.fftx = -1 if "fftx" not in controller.Command_Set else int(controller.Command_Set["fftx"])
        self.ffty = -1 if "ffty" not in controller.Command_Set else int(controller.Command_Set["ffty"])
        self.fftz = -1 if "fftz" not in controller.Command_Set else int(controller.Command_Set["fftz"])
        self.atom_numbers = md_info.atom_numbers
        self.box_length = md_info.box_length

        self.volume = self.box_length[0] * self.box_length[1] * self.box_length[1]

        if self.fftx < 0:
            self.fftx = self.Get_Fft_Patameter(self.box_length[0])
        if self.ffty < 0:
            self.ffty = self.Get_Fft_Patameter(self.box_length[1])
        if self.fftz < 0:
            self.fftz = self.Get_Fft_Patameter(self.box_length[2])
        print("    fftx: ", self.fftx)
        print("    ffty: ", self.ffty)
        print("    fftz: ", self.fftz)
        print("pme cutoff", self.cutoff)
        print("pme tolerance", self.tolerance)
        self.PME_Nall = self.fftx * self.ffty * self.fftz
        self.PME_Nin = self.ffty * self.fftz
        self.PME_Nfft = self.fftx * self.ffty * (int(self.fftz / 2) + 1)
        self.PME_inverse_box_vector = [self.fftx / self.box_length[0],
                                       self.ffty / self.box_length[1],
                                       self.fftz / self.box_length[2]]

        self.beta = self.Get_Beta(self.cutoff, self.tolerance)
        self.neutralizing_factor = -0.5 * self.CONSTANT_Pi / (self.beta * self.beta * self.volume)
        self.is_initialized = 1

    def Get_Beta(self, cutoff, tolerance):
        '''GET BETA'''
        high = 1.0
        ihigh = 1
        while 1:
            tempf = math.erfc(high * cutoff) / cutoff
            if tempf <= tolerance:
                break
            high *= 2
            ihigh += 1
        ihigh += 50
        low = 0.0
        for _ in range(1, ihigh):
            beta = (low + high) / 2
            tempf = math.erfc(beta * cutoff) / cutoff
            if tempf >= tolerance:
                low = beta
            else:
                high = beta
        return beta

    def Check_2357_Factor(self, number):
        '''CHECK FACTOR'''
        while number > 0:
            if number == 1:
                return 1
            tempn = int(number / 2)
            if tempn * 2 != number:
                break
            number = tempn
        while number > 0:
            if number == 1:
                return 1
            tempn = int(number / 3)
            if tempn * 3 != number:
                break
            number = tempn
        while number > 0:
            if number == 1:
                return 1
            tempn = int(number / 5)
            if tempn * 5 != number:
                break
            number = tempn
        while number > 0:
            if number == 1:
                return 1
            tempn = int(number / 7)
            if tempn * 7 != number:
                break
            number = tempn
        return 0

    def Get_Fft_Patameter(self, length):
        '''GET FFT PARAMETER'''
        tempi = math.ceil(length + 3) >> 2 << 2
        if 60 <= tempi <= 68:
            tempi = 64
        elif 120 <= tempi <= 136:
            tempi = 128
        elif 240 <= tempi <= 272:
            tempi = 256
        elif 480 <= tempi <= 544:
            tempi = 512
        elif 960 <= tempi <= 1088:
            tempi = 1024
        while 1:
            if self.Check_2357_Factor(tempi):
                return tempi
            tempi += 4
