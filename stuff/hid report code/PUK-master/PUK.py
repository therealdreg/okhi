#!/user/bin/python3

# https://regex101.com/r/PQ9g6g/6

import traceback
import os.path
import sys
import re
from maps import shift_map, mod_keys, base_keys

help_info = '''
  Usage: PUK.py [OPTIONS] [INPUT FILE]
    -o fileName.txt   Output file
    -v                Verbose  (Default if -o is not present.)
    -c                Confirm targets before file I/O.
    -t                Tuple mode (mod_key_1, ..., mod_key_N, base_key)
    -h                Help
    
  This program parses hex data from captured USB keyboard packets. Each line of input should have one base key and up to one mod key. Each line should be 4 or 8 bytes. Tuple mode is harder to read, but should offer more context if mod keys are important.

'''

# run options
cl_args = {}

# run states (environment assumptions)
run_state = None
'''
run_input_only = 0
run_options_input = 1
run_options_output_input = 2
'''

# Returns a dictionary with the results of parsing the command line args, and sets the program's run_state.
def parse_args():
  global run_state
  global cl_args
  cl_args_regex = '^(?P<HELP>-{0,2}(?:[ovt]*h[ovt]*(?:$| .*)|help.*))|(?:(?: |^)(?P<OPT>(?: ?-{0,2}[ovht])+)(?:(?: |^)(?P<OUT>[\w\-.]+|[\'\"][\w\-.:/\\ ]+[\'\"]))?)?(?: |^)(?P<IN>[\w\-.:/\\ ]+|[\'\"][\w\-.:/\\ ]+[\'\"])?$'
  cl_args = ' '.join(sys.argv[1:])
  cl_args_matches = re.search(cl_args_regex, cl_args)
  cl_args = {
    'HELP' : cl_args_matches.group('HELP'),
    'OPT' : cl_args_matches.group('OPT'),
    'OUT' : cl_args_matches.group('OUT'),
    'IN' : cl_args_matches.group('IN'),
    'ERROR' : False,
    'o' : False,
    'v' : False,
    'c' : False,
    'h' : False,
    't' : False
  }
  
  # Was the input file specified?
  if cl_args['IN']:
    # Were any options specified?
    if cl_args['OPT']:
      # Attempt to parse option flags.
      for i in cl_args['OPT']:
        if i not in ' -ovcht':
          cl_args['ERROR'] = True
          break
        if i in cl_args:
          cl_args[i] = True
      # Check if an output file was specified.
      if not cl_args['ERROR']:
        # Output file expected.
        if cl_args['o']:
          if not cl_args['OUT']:
            cl_args['ERROR'] = True
          run_state = 'run_options_output_input'
        else:
          # Output file not expected.
          if cl_args['OUT']:
            cl_args['ERROR'] = True
          run_state = 'run_options_input'
    else:
      run_state = 'run_input_only'
  else:
    cl_args['ERROR'] = True
  return not cl_args['ERROR']



def confirm_inputs(opt, in_file, out_file):
  _ = input(f'\nPlease confirm the following targets:\n  Options: {opt}\n  Input file: {in_file}\n  Output file: {out_file}\n\n  (Y/n)> ')
  if _ == 'Y':
    return True
  elif _ == 'y':
    if input('\nDid you mean "Y"?\n  (y/n) > ').lower() == 'y':
      return True
  return False



def parse_hex(line):
  hex_regex = '^(?P<MOD>[0-9a-f]{2})00(?P<KEY>[0-9a-f]{2}).*$'
  hex_matches = re.search(hex_regex, line.lower())
  return hex_matches.group('MOD'), hex_matches.group('KEY')
  
  
  
def translate_hex(t, mod_key_pair):
  mod, key = mod_key_pair
  
  if mod in mod_keys:
    mod = mod_keys[mod]
  else:
    return ''
  if key in base_keys:
    key = base_keys[key]
  else:
    return ''
  if (mod == 'left_shift' or mod == 'right_shift') and key in shift_map:
    key = shift_map[key]
 
  if t:
    return f'({mod}, {key})'
  return key



def main():
  if not parse_args() or (cl_args['c'] and not confirm_inputs(cl_args['OPT'], cl_args['IN'], cl_args['OUT'])):
    print(help_info)
  else:
    print('')
    try:
      with open(cl_args['IN'], 'r') as in_file:
        if run_state == 'run_input_only' or run_state == 'run_options_input':
          for line in in_file:
            print(translate_hex(cl_args['t'], parse_hex(line)), end="")
        else: #run_options_output_input
          with open(cl_args['OUT'], 'w') as out_file:
            for line in in_file:
              _ = (translate_hex(cl_args['t'], parse_hex(line)))
              if cl_args['v']:
                print(_, end="")
              out_file.write(f'{_}')
    except Exception as e:
      print(f"\nSomething went wrong. Please check the target files.\n  Options: {cl_args['OPT']}\n  Input file: {cl_args['IN']}\n  Output file: {cl_args['OUT']}\n")
      #traceback.print_exc()
  #print(cl_args, run_state)



if __name__ == '__main__':
  main()
