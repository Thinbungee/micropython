#!/usr/bin/env python
"""Generates the pin_cap table file for the SAMD port."""

from __future__ import print_function

import argparse
import sys
import csv

table_header = """// This file was automatically generated by make-pin-cap.py
//

// The Pin objects which are available ona board

"""


class Pins:
    def __init__(self):
        self.board_pins = []  # list of pin objects
        self.pin_names = {}

    def parse_csv_file(self, filename):
        with open(filename, "r") as csvfile:
            rows = csv.reader(csvfile)
            for row in rows:
                # Pin numbers must start with "PA", "PB", "PC" or "PD"
                if len(row) > 0 and row[0].strip().upper()[:2] in ("PA", "PB", "PC", "PD"):
                    self.board_pins.append(row)

    def parse_pin_file(self, filename):
        with open(filename, "r") as csvfile:
            rows = csv.reader(csvfile, skipinitialspace=True)
            for row in rows:
                # Pin numbers must start with "PIN_"
                # LED numbers must start with "LED_"
                if len(row) > 0:
                    # for compatibility, map LED_ to PIN_
                    if row[0].startswith("LED_"):
                        row[0] = "PIN_" + row[0][4:]
                    if row[0].startswith("PIN_"):
                        if len(row) == 1:
                            self.pin_names[row[0]] = (row[0][4:], "{&machine_pin_type}")
                        else:
                            self.pin_names[row[0]] = (row[1], "{&machine_pin_type}")

    def print_table(self, table_filename, mcu_name):
        with open(table_filename, "wt") as table_file:
            table_file.write(table_header)

            # Create the Pin objects

            if mcu_name == "SAMD21":
                for row in self.board_pins:
                    pin = "PIN_" + row[0].upper()
                    table_file.write("#ifdef " + pin + "\n")
                    table_file.write("static const machine_pin_obj_t %s_obj = " % pin)
                    eic = row[1] if row[1] else "0xff"
                    adc = row[2] if row[2] else "0xff"
                    if pin in self.pin_names:
                        name = "MP_QSTR_%s" % self.pin_names[pin][0]
                        type = self.pin_names[pin][1]
                    else:
                        name = "MP_QSTR_"
                        type = "{&machine_pin_type}"
                    table_file.write("{%s, %s, %s, %s, %s" % (type, pin, name, eic, adc))
                    for cell in row[3:]:
                        if cell:
                            table_file.write(
                                ", 0x%s" % cell if len(cell) == 2 else ", 0x0%s" % cell
                            )
                        else:
                            table_file.write(", 0xff")
                    table_file.write("};\n")
                    table_file.write("#endif\n")
            else:
                for row in self.board_pins:
                    pin = "PIN_" + row[0].upper()
                    table_file.write("#ifdef " + pin + "\n")
                    table_file.write("const machine_pin_obj_t %s_obj = " % pin)
                    eic = row[1] if row[1] else "0xff"
                    adc0 = row[2] if row[2] else "0xff"
                    adc1 = row[3] if row[3] else "0xff"
                    if pin in self.pin_names:
                        name = "MP_QSTR_%s" % self.pin_names[pin][0]
                        type = self.pin_names[pin][1]
                    else:
                        name = "MP_QSTR_"
                        type = "{&machine_pin_type}"
                    table_file.write(
                        "{%s, %s, %s, %s, %s, %s" % (type, pin, name, eic, adc0, adc1)
                    )
                    for cell in row[4:]:
                        if cell:
                            table_file.write(
                                ", 0x%s" % cell if len(cell) == 2 else ", 0x0%s" % cell
                            )
                        else:
                            table_file.write(", 0xff")
                    table_file.write("};\n")
                    table_file.write("#endif\n")

            # Create the Pin table

            table_file.write("\n// The table of references to the pin objects.\n\n")
            table_file.write("static const machine_pin_obj_t *pin_af_table[] = {\n")
            for row in self.board_pins:
                pin = "PIN_" + row[0].upper()
                table_file.write("    #ifdef " + pin + "\n")
                table_file.write("    &%s_obj,\n" % pin)
                table_file.write("    #endif\n")
            table_file.write("};\n")

            # Create the CPU pins dictionary table

            table_file.write("\n#if MICROPY_HW_PIN_BOARD_CPU\n")
            table_file.write("\n// The cpu pins dictionary\n\n")
            table_file.write(
                "STATIC const mp_rom_map_elem_t pin_cpu_pins_locals_dict_table[] = {\n"
            )
            for row in self.board_pins:
                pin = "PIN_" + row[0].upper()
                table_file.write("    #ifdef " + pin + "\n")
                table_file.write(
                    "    { MP_ROM_QSTR(MP_QSTR_%s), MP_ROM_PTR(&%s_obj) },\n"
                    % (row[0].upper(), pin)
                )
                table_file.write("    #endif\n")
            table_file.write("};\n")
            table_file.write(
                "MP_DEFINE_CONST_DICT(machine_pin_cpu_pins_locals_dict, pin_cpu_pins_locals_dict_table);\n"
            )

            # Create the board pins dictionary table

            table_file.write("\n// The board pins dictionary\n\n")
            table_file.write(
                "STATIC const mp_rom_map_elem_t pin_board_pins_locals_dict_table[] = {\n"
            )
            for row in self.board_pins:
                pin = "PIN_" + row[0].upper()
                if pin in self.pin_names:
                    table_file.write("    #ifdef " + pin + "\n")
                    table_file.write(
                        "    { MP_ROM_QSTR(MP_QSTR_%s), MP_ROM_PTR(&%s_obj) },\n"
                        % (self.pin_names[pin][0], pin)
                    )
                    table_file.write("    #endif\n")
            table_file.write("};\n")
            table_file.write(
                "MP_DEFINE_CONST_DICT(machine_pin_board_pins_locals_dict, pin_board_pins_locals_dict_table);\n"
            )
            table_file.write("#endif\n")


def main():
    parser = argparse.ArgumentParser(
        prog="make-pin-af.py",
        usage="%(prog)s [options] [command]",
        description="Generate MCU-specific pin cap table file",
    )
    parser.add_argument(
        "-c",
        "--csv",
        dest="csv_filename",
        help="Specifies the pin-af-table.csv filename",
    )
    parser.add_argument(
        "-b",
        "--board",
        dest="pin_filename",
        help="Specifies the pins.csv filename",
    )
    parser.add_argument(
        "-t",
        "--table",
        dest="table_filename",
        help="Specifies the name of the generated pin cap table file",
    )
    parser.add_argument(
        "-m",
        "--mcu",
        dest="mcu_name",
        help="Specifies type of the MCU (SAMD21 or SAMD51)",
    )
    args = parser.parse_args(sys.argv[1:])

    pins = Pins()

    if args.csv_filename:
        pins.parse_csv_file(args.csv_filename)

    if args.pin_filename:
        pins.parse_pin_file(args.pin_filename)

    if args.table_filename:
        pins.print_table(args.table_filename, args.mcu_name)


if __name__ == "__main__":
    main()
