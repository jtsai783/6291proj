#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string>
#include <algorithm>
#include <vector>

std::vector<std::string> prologue;
std::vector<std::string> epilogue;

/*
Read instructions from an input file and return all instructions 
*/
std::vector<std::string> readInput(const char* fname)
{
    std::string str;
    std::vector<std::string> instructions;
    
    std::ifstream filestream(fname);
    
    while (std::getline(filestream, str))
    {
        if (epilogue.empty()) {
            std::size_t pos = str.find("\tc0    ");
            if (pos == 0) {
                instructions.push_back(str);
            } else {
                pos = str.find(";;");
                if (pos == 0)
                    instructions.push_back(str);
                else {
                    pos = str.find(".call printf");
                    if (pos == 0) {
                        epilogue.push_back(str);
                    } else {
                        if (!instructions.empty()) {
                copy(instructions.begin(), instructions.end(), back_inserter(prologue));
                            instructions.clear();
                        }
                        prologue.push_back(str);
                    }
                }
            }
        } else {
            epilogue.push_back(str);
        }
    }
   
   return instructions;
}

/*
Print scheduled VLIW instructions to a file.
*/
void printOutput(const char* fname, std::vector<std::string> scheduledVLIW)
{
    std::ofstream outfile;
    outfile.open(fname);
  
    for (int i = 0; i < prologue.size(); i++)
        outfile << prologue[i] << "\n";

    for (int i = 0; i < scheduledVLIW.size(); i++)
        outfile << scheduledVLIW[i] << "\n";

    for (int i = 0; i < epilogue.size(); i++)
        outfile << epilogue[i] << "\n";

    outfile.close();
}

/*
TODO : Write any helper functions that you may need here. 

*/
#include <unordered_map>
#include <regex>


// whether str1 contains str2
bool strContains(std::string str1, std::string str2) {
  return str1.find(str2) != std::string::npos;
}

// Author: Benjamin Greenwood
// create a vector of meaningful components of the instruction - operation, write location, equals sign, read locations
std::vector<std::string> splitInstruction(std::string instruction) {
  std::vector<std::string> instructionComponents;
  std::size_t pos;

  while(strContains(instruction, "  ")) // remove consecutive spaces.
    instruction.replace(instruction.find("  "), 2, " ");

  pos = instruction.find("\tc0 "); // regular instructions...
  if(pos != std::string::npos) {
    instruction = instruction.substr(pos + 4);

    pos = instruction.find("##"); // stop instructions at comment start
    if(pos != std::string::npos)
      instruction = instruction.substr(0, pos - 1);

    while(instruction[instruction.size() - 1] == ' ') // remove spaces on the tail
      instruction.pop_back();

    while(!instruction.empty()) {
      pos = instruction.find(" "); // split instruction around spaces
      if(pos != std::string::npos) {
        std::string component = instruction.substr(0, pos);
        if(component[component.size() - 1] == ',') // remove commas from argument list
          component.pop_back();

        instructionComponents.push_back(component);
        instruction = instruction.substr(pos + 1);
      } else {
        instructionComponents.push_back(instruction); // the final word that doesn't have a space after it.
        instruction = "";
      }
    }
  }

  return instructionComponents;
}

// Author: Benjamin Greenwood
class dependence_node {
    std::vector<dependence_node* > dependents; // instructions that depend on this one
    std::vector<dependence_node* > parents; // instruction that this one depends on
    int latency;
    int height; // length of longest path to end from this node
    int height_jim = 0;
    float criticality;
    dependence_node* critical_dependent; // the dependent that leads to this instruction's longest path out
    // dependence_node* critical_leaf; // the node at the end of this instruction's longest path out
    int instruction_number; // in the order of the original code
    int original_index; // index in the original instructions vector
    int cycle_scheduled; // the cycle in which this instruction gets scheduled
    std::unordered_map<std::string, int> reservation_table;
    // std::unordered_map<std::string, int>* global_rt;
    std::vector<std::string> reads_from; // locations / registers this instruction reads from
    std::string writes_to; // location / register this instruction reads to
    std::string operation; // operation this instruction perform
    std::string mem_base; // base and offset for memory operation
    std::string mem_offset;

  public:
    dependence_node(std::string op, std::string wr, std::vector<std::string> re, 
    int num, int ind) : operation(op), writes_to(wr), reads_from(re), instruction_number(num),
    original_index(ind), cycle_scheduled(-1) {
      
      // parse memory locations (e.g. 0x18[$r0.1])
      std::size_t pos;
      if(strContains(wr, "[") and strContains(wr, "]")) {
        pos = wr.find("[");
        mem_base = wr.substr(pos + 1, wr.find("]") - (pos + 1));
        mem_offset = wr.substr(0, pos - 1);
        writes_to = "mem";
      } else {
        for(int i = 0; i < re.size(); i++) {
          std::string read_loc = re[i];
          if(strContains(read_loc, "[") and strContains(read_loc, "]")) {
            pos = read_loc.find("[");
            mem_base = read_loc.substr(pos + 1, read_loc.find("]") - (pos + 1));
            mem_offset = read_loc.substr(0, pos - 1);
            reads_from[i] = "mem";
          }
        }
      }

      if (strContains(op, "add") or strContains(op, "mov") or strContains(op, "cmp") or strContains(op, "slct")) {
        latency = 1;
        reservation_table["ALU"] = 1;
        reservation_table["MUL"] = 0;
        reservation_table["MEM"] = 0;
        // reservation_table["COPY"] = 0;
        // reservation_table["OPERATIONS"] = 1;
      } else if (strContains(op, "mpy")) {
        latency = 2;
        reservation_table["ALU"] = 0;
        reservation_table["MUL"] = 1;
        reservation_table["MEM"] = 0;
        // reservation_table["COPY"] = 0;
        // reservation_table["OPERATIONS"] = 1;
      } else if (strContains(op, "ldw")) {
        latency = 3;
        reservation_table["ALU"] = 0;
        reservation_table["MUL"] = 0;
        reservation_table["MEM"] = 1;
        // reservation_table["COPY"] = 0;
        // reservation_table["OPERATIONS"] = 1;
      } else if (strContains(op, "stw")) {
        latency = 1;
        reservation_table["ALU"] = 0;
        reservation_table["MUL"] = 0;
        reservation_table["MEM"] = 1;
        // reservation_table["COPY"] = 0;
        // reservation_table["OPERATIONS"] = 1;
      // } else if (strContains(op, "mov")) {
      //   latency = 1;
      //   reservation_table["ALU"] = 0;
      //   reservation_table["MUL"] = 0;
      //   reservation_table["MEM"] = 0;
      //   reservation_table["COPY"] = 1;
      //   reservation_table["OPERATIONS"] = 2;
      } else {
        std::cout << "WARNING: UNRECOGNIZED INSTRUCTION: " << op << "\n";
        latency = 0;
      }

      height = latency; // height will increase as dependents are added
      // critical_leaf = this;


    }

    std::string get_write_location() const { return this->writes_to; }
    void set_write_location(std::string new_reg) { writes_to = new_reg; }
    std::vector<std::string> get_read_locations() const { return this->reads_from; }
    void replace_read(std::string old_read, std::string new_read) {
      for(int i = 0; i < reads_from.size(); i++) {
        if(reads_from[i] == old_read)
          reads_from[i] = new_read;
      }
    }
    std::string get_mem_base() const { return this->mem_base; }
    void set_mem_base(std::string new_base) { mem_base = new_base; }
    std::string get_mem_offset() const { return this->mem_offset; }
    std::vector<dependence_node* > get_dependents() const { return this->dependents; }
    std::vector<dependence_node* > get_parents() const { return this->parents; }
    int get_latency() const { return this->latency; }
    int get_height() const { return this->height; }
    int get_height_jim() const {return this->height_jim;};
    int get_cycle_scheduled() const { return this->cycle_scheduled; }
    void set_cycle_scheduled(int cycle) { cycle_scheduled = cycle; }
    bool is_scheduled() const { return cycle_scheduled != -1; }
    void set_height(int new_height) { this->height = new_height; }
    void set_height_jim(int h) {this->height_jim = h;};
    dependence_node* get_critical_dependent() const { return this->critical_dependent; }
    // dependence_node* get_critical_leaf() const { return this->critical_leaf; }
    void set_critical_dependent(dependence_node* dep) { this->critical_dependent = dep; }
    int get_instruction_number() const { return this->instruction_number; }
    int get_original_index() const { return this->original_index; }
    std::string get_operation() const { return this->operation; }
    std::unordered_map<std::string, int> get_rt() const { return reservation_table; }
    void set_criticality(float c) {this->criticality = c;}
    float get_criticality() {return this->criticality;}



    // traverses towards the roots (instructions with no dependents), updating height if it has increased
    void update_height(dependence_node* dependent) {
      int path_length = latency + dependent->get_height();

      if (height < path_length) {//and critical_leaf != dependent->get_critical_leaf()) { // critical leaf prevents height from being updated multiple times per added instruction
        height = path_length;
        critical_dependent = dependent;
        // critical_leaf = dependent->get_critical_leaf();

        // traverse up the roots
        std::vector<dependence_node* > grandparents = this->get_parents();
        for(int i = 0; i < grandparents.size(); i++) {
          grandparents[i]->update_height(this);
        }
      }
    }

    // if dependent's height gets reduced, traverse up tree and reduce parent's heights if needed.
    void height_lost(dependence_node* dependent) {
      if(dependent == critical_dependent) {
        int max_height = 0;

        for(dependence_node* d : dependents) {
          if(d->get_height() > max_height) {
            max_height = d->get_height();
            critical_dependent = d;
          }
        }

        height = latency + max_height;
        for(dependence_node* p : parents)
          p->height_lost(this);
      }
    }

    // adds dependent - prevents the same node from appearing twice in list.
    void add_dependent(dependence_node* dependent) {
      for(int i = 0; i < dependents.size(); i++) // prevent repeat recordings
        if(dependents[i] == dependent) return;

      dependents.push_back(dependent);
      this->update_height(dependent);
    }

    void remove_dependent(dependence_node* to_remove) {
      for(auto d = dependents.begin(); d < dependents.end(); d++) {
        if(to_remove == *d) {
          dependents.erase(d);
          break;
        }
      }

      height_lost(to_remove);
    }

    void add_parent(dependence_node* parent) {
      for(int i = 0; i < parents.size(); i++) // prevent repeat recordings
        if(parents[i] == parent) return;

      parents.push_back(parent);
      // parent->update_height(this);
    }

    void remove_parent(dependence_node* to_remove) {
      for(auto p = parents.begin(); p < parents.end(); p++) {
        if (to_remove == *p) {
          parents.erase(p);
          break;
        }
      }
    }

    // whether this instruction depends on other instructions
    bool is_not_dependent() {
      return parents.size() == 0;
    }

    void print() {
      std::cout << operation << " " << writes_to << " = ";
      for(int i = 0; i < reads_from.size(); i++) {
        std::cout << reads_from[i] << " ";
      }
      std::cout << std::endl;
    }


};

// Author: Jim Tsai
bool isMemLocation(std::string operand) {
  std::size_t index;
  index = operand.find("[");
  if(index != std::string::npos)
    return true;
  if(operand == "mem") // Ben Greenwood
    return true; // Ben Greenwood
  return false;
}

// Author: Jim Tsai
std::string getMemLocBase(std::string operand) {
  std::regex r("\\[(.+)\\]");
  std::smatch m;
  std::regex_search(operand, m, r);
  return m[1];
}

// Author: Jim Tsai
std::string getMemLocOffset(std::string operand) {
  std::regex r("(.+)\\[.+\\]");
  std::smatch m;
  std::regex_search(operand, m, r);
  return m[1];
}

// Author: Jim Tsai
bool has_dependency(dependence_node* prev, dependence_node* current) {
  std::string prev_wr, curr_wr;
  prev_wr = prev->get_write_location();
  curr_wr = current->get_write_location();

  std::vector<std::string> current_reads;
  current_reads = current->get_read_locations();

  std::vector<std::string> prev_reads;
  prev_reads = prev->get_read_locations();

  //register WAW
  if (!isMemLocation(prev_wr) && !isMemLocation(curr_wr) && (prev_wr == curr_wr) ) {
    return true;
  }

  //register WAR
  if (!isMemLocation(curr_wr)) {
    for(auto r : prev_reads) { 
      if (isMemLocation(r)) {
        if(getMemLocBase(r) == curr_wr) {
          return true;
        }
      } else {
        if( r == curr_wr ) {
          return true;
        }
      }
    }
  }

  //register RAW
  if(!isMemLocation(prev_wr)) {
    for( auto cr : current_reads ) {
      if(!isMemLocation(cr)) {
        if (cr == prev_wr) {
          return true;
        }
      } else {
        std::string cr_base;
        cr_base = getMemLocBase(cr);
        if (cr_base == prev_wr) {
          return true;
        }
      }
    }
  }
  
  //memory WAW
  //no dependence: same base, diff offset, right after another
  //if any of the 3 above conditions are violated, assume WAW
  if(isMemLocation(prev_wr) && isMemLocation(curr_wr)) {
    if (getMemLocBase(prev_wr) != getMemLocBase(curr_wr)) { //diff base, assume mem waw
      return true;
    } else if (getMemLocOffset(prev_wr) == getMemLocOffset(curr_wr)) { //same offset, assume mem waw
      return true;
    } else if(prev->get_instruction_number() + 1 != current->get_instruction_number()) { //current not follwing immediate after, assume mem waw
      return true;
    }
  }

  //memory RAW, similar to above
  for(auto cr : current_reads){
    if(isMemLocation(cr) && isMemLocation(prev_wr)) {
      if(getMemLocBase(cr) != getMemLocBase(prev_wr)) {
        return true;
      } else if (getMemLocOffset(cr) == getMemLocOffset(prev_wr)) {
        return true;
      } else if (prev->get_instruction_number() + 1 != current->get_instruction_number()) {
        return true;
      }
    }
  }

  //memory WAR
  for(auto pr : prev_reads) {
    if(isMemLocation(curr_wr) && isMemLocation(pr)) {
      if(getMemLocBase(curr_wr) != getMemLocBase(pr)) {
        return true;
      } else if (getMemLocOffset(curr_wr) == getMemLocOffset(pr)) {
        return true;
      } else if (prev->get_instruction_number() + 1 != current->get_instruction_number()) {
        return true;
      }
    }
  }

  return false;
}

//Jim Tsai
bool source_comp (dependence_node* i,dependence_node* j) {
  return i->get_instruction_number() < j->get_instruction_number();
}

bool crit_comp (dependence_node* i,dependence_node* j) {
  return i->get_criticality() < j->get_criticality();
}

bool height_comp (dependence_node* i,dependence_node* j) {
  return i->get_height_jim() < j->get_height_jim();
}

bool fan_comp(dependence_node* i,dependence_node* j) {
  return i->get_dependents().size() < j->get_dependents().size();
}

// Author: Benjamin Greenwood
class annotated_edge {
  public:
    class annotation {
      public:
        int delay;
        std::string shared_register;
        bool type;

        annotation(int del, std::string reg, bool typ) : delay(del), shared_register(reg), type(typ) {}
    };
  
    dependence_node* parent;
    dependence_node* dependent;
    std::vector<annotation> annotations;
    // int delay;
    // std::string shared_register;
    // bool type;
    
    // constructor adapted from Jim Tsai's "has_dependency"
    annotated_edge(dependence_node* prev, dependence_node* current) : parent(prev), dependent(current) {//, type(true) {
      std::string prev_wr = parent->get_write_location();
      std::string curr_wr = dependent->get_write_location();
      std::vector<std::string> prev_reads = parent->get_read_locations();
      std::vector<std::string> current_reads = dependent->get_read_locations();


      //memory RAW
      //no dependence: same base, diff offset, right after another
      //if any of the 3 above conditions are violated, we have a dependence, either MAY or MUST
      for(auto cr : current_reads){
        if(isMemLocation(cr) && isMemLocation(prev_wr)) {
          if(dependent->get_mem_base() != parent->get_mem_base()) { // diff base, we have MAY dependence
            // shared_register = "-";
            // type = false;
            annotations.push_back(annotation(0, "-", false));
          } else if (dependent->get_mem_offset() == parent->get_mem_offset()) { // same offset, we have MUST dependence
            // shared_register = "-";
            // type = true;
            annotations.push_back(annotation(0, "-", true));
          } else if (prev->get_instruction_number() + 1 != current->get_instruction_number()) { // dependent not immediately flowing parent, we have MAY dependence
            // shared_register = "-";
            // type = false;
            annotations.push_back(annotation(0, "-", false));
          }
        }
      } 

      //register WAW
      if (!isMemLocation(prev_wr) && !isMemLocation(curr_wr) && (prev_wr == curr_wr) ) {
        // type = false;
        // shared_register = prev_wr;
        annotations.push_back(annotation(0, prev_wr, false));
      }

      //register WAR
      if (!isMemLocation(curr_wr)) {
        for(auto r : prev_reads) { 
          if (isMemLocation(r)) {
            if(parent->get_mem_base() == curr_wr) {
              // if (type == false) { // if we've already identified a MAY dependence here, we are overwriting it now
              //   std::cout << "MAY dependence being overwritten - will need to reannotate edges after register renaming\n";
              // }
              // type = false;
              // shared_register = curr_wr;
              annotations.push_back(annotation(0, curr_wr, false));
            }
          } else {
            if( r == curr_wr ) {
              // type = false;
              // shared_register = curr_wr;
              annotations.push_back(annotation(0, curr_wr, false));
            }
          }
        }
      }

      //register RAW
      if(!isMemLocation(prev_wr)) {
        for( auto cr : current_reads ) {
          if(!isMemLocation(cr)) {
            if (cr == prev_wr) {
              // type = true;
              // shared_register = cr;
              annotations.push_back(annotation(0, cr, true));
            }
          } else {
            std::string cr_base = dependent->get_mem_base();
            if (cr_base == prev_wr) {
              // type = true;
              // shared_register = cr_base;
              annotations.push_back(annotation(0, cr_base, true));
            }
          }
        }
      }
      
      // memory WAW
      // DONT hoist stores above stores!!!!
      // if(isMemLocation(prev_wr) && isMemLocation(curr_wr)) {
      //   if (parent->get_mem_base() != dependent->get_mem_base()) { //diff base, we have MAY dependence
      //     annotations.push_back(annotation(0, "-", false));
      //   } else if (parent->get_mem_offset() == dependent->get_mem_offset()) { //same offset, we have MUST dependence
      //     annotations.push_back(annotation(0, "-", true));
      //   } else if(prev->get_instruction_number() + 1 != current->get_instruction_number()) { //current not following immediate after, we have MAY dependence
      //     annotations.push_back(annotation(0, "-", false));
      //   }
      // }


      // memory WAR - don't hoist stores above loads!!!
      // for(auto pr : prev_reads) {
      //   if(isMemLocation(curr_wr) && isMemLocation(pr)) {
      //     if(dependent->get_mem_base() != parent->get_mem_base()) {
      //       annotations.push_back(annotation(0, "-", false));
      //     } else if (dependent->get_mem_offset() == parent->get_mem_offset()) {
      //       annotations.push_back(annotation(0, "-", true));
      //     } else if (prev->get_instruction_number() + 1 != current->get_instruction_number()) {
      //       annotations.push_back(annotation(0, "-", false));
      //     }
      //   }
      // }

    }

    void register_renamed(std::string shared_reg) {
      for(auto it = annotations.begin(); it < annotations.end(); it++) {
        if ((*it).shared_register == shared_reg and (*it).type == false) {
          annotations.erase(it--);
        }
      }
    }

};

// strContains that accounts for instruction text formats
bool inst_contains_reg(std::string inst, std::string reg) {
  return strContains(inst, reg + " ") or strContains(inst, reg + ",") or strContains(inst, reg + "[");
}

// replace a register in an instruction
std::string find_replace_register(std::string old_reg, std::string new_reg, std::string instruction) {

  if(strContains(instruction, old_reg + " ")) {
    instruction.replace(instruction.find(old_reg + " "), old_reg.size(), new_reg);
  } else if(strContains(instruction, old_reg + ",")) {
    instruction.replace(instruction.find(old_reg + ","), old_reg.size(), new_reg);
  } else if(strContains(instruction, old_reg + "]")) {
    instruction.replace(instruction.find(old_reg + "]"), old_reg.size(), new_reg);
  }

  return instruction;
}


// Author: Benjamin Greenwood
class dependence_graph {
    std::vector<annotated_edge* > edges; // annotated edges for register renaming and speculation
    std::vector<dependence_node* > roots; // list of nodes with no incoming edges (no dependencies)
    std::vector<dependence_node* > sequence; // vector of all nodes, in the graph, order of source code, contributed by Jim Tsai
    std::vector<dependence_node* > topological_ordering; // vector of all nodes in prioritized topological order.
    std::vector<std::unordered_map<std::string, int> > global_rt; // vector of cycles - each cycle has an RT indicating how many of each resource is available that cycle
    std::vector<std::string> instructions; // original vector of instruction text
    std::vector<std::string> free_registers; // registers available for register renaming
    int instruction_count; // count of instructions
    int mode;

  public:
    dependence_graph(std::vector<std::string> instructions, int mode) : instructions(instructions), instruction_count(0), mode(mode) {
      add_cycle();

      for(int r = 56; r >= 3; r--) {
        free_registers.push_back("$r0." + std::to_string(r));
      }

      for(int i = 0; i < instructions.size(); i++) {
        std::vector<std::string> instructionComponents = splitInstruction(instructions[i]);
        // for(int j = 0; j < instructionComponents.size(); j++)
        //   std::cout << instructionComponents[j];

        if (instructionComponents.size() > 0) {
          // std::cout << std::endl;
          std::string op = instructionComponents[0];
          std::string wr = instructionComponents[1];
          std::vector<std::string> re(instructionComponents.begin() + 3, instructionComponents.end());

          this->add_instruction(op, wr, re, i);
        }
      }
    }

    // copy constructor, copy/paste to avoide c++11 errors
    dependence_graph(const dependence_graph& rhs) : instructions(rhs.get_instructions()), instruction_count(0), mode(rhs.get_mode()) {
      add_cycle();

      for(int r = 56; r >= 3; r--) {
        free_registers.push_back("$r0." + std::to_string(r));
      }

      for(int i = 0; i < instructions.size(); i++) {
        std::vector<std::string> instructionComponents = splitInstruction(instructions[i]);

        // for(int j = 0; j < instructionComponents.size(); j++)
        //   std::cout << instructionComponents[j];

        if (instructionComponents.size() > 0) {
          // std::cout << std::endl;
          std::string op = instructionComponents[0];
          std::string wr = instructionComponents[1];
          std::vector<std::string> re(instructionComponents.begin() + 3, instructionComponents.end());

          this->add_instruction(op, wr, re, i);
        }
      }
    };

    std::vector<dependence_node* > get_roots() const { return this->roots; }
    std::vector<dependence_node* > get_sequence() { return this->sequence; }
    std::vector<dependence_node* > get_topological_ordering() { return this->topological_ordering; }
    // returns original text of program
    std::vector<std::string> get_instructions() const { return this->instructions; }
    int get_mode() const { return mode; }

    // just removes (first instance of) register from the list of free registers
    // doesn't check if it's actually in the list of free registers
    void ralloc(std::string reg_name) {
      for(auto it = free_registers.end() - 1; it >= free_registers.begin(); it--) {
        if(*it == reg_name) {
          free_registers.erase(it);
          break;
        }
      }
    }
    
    // adds instruction to graph and records all dependencies, latencies, and heights
    // refactored by Jim Tsai and Benjamin Greenwood
    void add_instruction(std::string op, std::string wr, std::vector<std::string> re, int index) {
      dependence_node* instruction_node = new dependence_node(op, wr, re, instruction_count, index);

      for(auto i : sequence) {
        if(has_dependency(i, instruction_node)) {
          instruction_node->add_parent(i);
          i->add_dependent(instruction_node);

          annotated_edge* edge = new annotated_edge(i, instruction_node);
          edges.push_back(edge);
        }
      }

      // remove used registers from the free list
      if(!isMemLocation(instruction_node->get_write_location())) {
        ralloc(instruction_node->get_write_location());
      }

      std::vector<std::string> reads = instruction_node->get_read_locations();
      for(std::string read : reads) {
        if(!isMemLocation(read)) {
          ralloc(read);
        }
      }

      //then we add it to the sequence
      sequence.push_back(instruction_node);
      instruction_count++;

      //and then we shove the root into the graph
      if (instruction_node->is_not_dependent())
        roots.push_back(instruction_node);

    }

    // Author: Benjamin Greenwood
    // adds a new cycle to the global reservation table
    void add_cycle() {
      std::unordered_map<std::string, int> cycle;
      cycle["ALU"] = 4;
      cycle["MUL"] = 2;
      cycle["MEM"] = 1;
      // cycle["COPY"] = 1;
      cycle["OPERATIONS"] = 4;
      global_rt.push_back(cycle);
    }

    // Author: Benjamin Greenwood
    void rename_registers() {
      // std::cout << "num edges: " << edges.size() << "\n";
      for(auto ed_it = edges.begin(); ed_it < edges.end(); ed_it++) {
        annotated_edge* edge = *ed_it;

        for(auto ann_it = edge->annotations.begin(); ann_it < edge->annotations.end(); ann_it++) {
          annotated_edge::annotation ann = *ann_it;

          if(!ann.type and ann.shared_register != "-") { // false dependency and register dependency
            std::string old_register = ann.shared_register;
            dependence_node* parent = edge->parent;
            dependence_node* dependent = edge->dependent;

            // std::cout << "Shared reg: " << old_register << "\n";
            // std::cout << "Parent: " << instructions[parent->get_original_index()] << "\n";
            // std::cout << "Dependent: " << instructions[dependent->get_original_index()] << "\n";

            if(!free_registers.empty()) {
              std::string new_register = free_registers.back();
              free_registers.pop_back();

              // since it's either a WAR or a WAW, we want to change the dependent's write location.
              dependent->set_write_location(new_register);

              // replace the write in the original instruction (will be the first instance found)
              if(inst_contains_reg(instructions[dependent->get_original_index()], old_register)) {
                instructions[dependent->get_original_index()] = find_replace_register(old_register, new_register, instructions[dependent->get_original_index()]);
              } else { std::cout << "This really shouldn't happen\n"; }

              // erase annotations
              edge->annotations.erase(ann_it--);
              if(edge->annotations.empty()) { // if no annotations left, remove edge and the whole relationship
                edges.erase(ed_it--);
                parent->remove_dependent(dependent);
                dependent->remove_parent(parent);
              }

              // have to rename the register in all instructions after the dependent as well.
              for(auto ed_it2 = ed_it + 1; ed_it2 < edges.end(); ed_it2++) {
                annotated_edge* edge2 = *ed_it2;
                dependence_node* parent2 = edge2->parent;

                dependence_node* dependent2 = edge2->dependent;

                // if this instructions comes after the other one, we have to rename the shared register
                if(dependent2->get_instruction_number() > dependent->get_instruction_number()) {
                  std::string writes_to = dependent2->get_write_location();
                  std::string mem_base = dependent2->get_mem_base();

                  if(writes_to == old_register)
                    dependent2->set_write_location(new_register);
                  dependent2->replace_read(old_register, new_register);
                  if(mem_base == old_register)
                    dependent2->set_mem_base(new_register);

                  // replace all occurances in original instruction
                  while(inst_contains_reg(instructions[dependent2->get_original_index()], old_register)) {
                    instructions[dependent2->get_original_index()] = find_replace_register(old_register, new_register, instructions[dependent2->get_original_index()]);
                  }
                }

                edge2->register_renamed(old_register); // this will remove (all) related annotation
                // we can only erase in this way because of the order in which we made the edges!
                if(edge2->annotations.empty()) {
                  edges.erase(ed_it2--);
                  parent2->remove_dependent(dependent2);
                  dependent->remove_parent(parent2);
                }
              } 
            } 
          }
        }
      }
      // for(std::string free : free_registers) {
      //   std::cout << free << "\n";
      // }
    }

    // Author: Benjamin Greenwood
    // implementation of Kahn's algorithm - source: Wikipedia
    void topological_sort(int mode) {
      dependence_graph dummy_graph(*this); // create a copy that we can dismantle while sorting

      // S ← Set of all nodes with no incoming edge
      std::vector<dependence_node* > S = dummy_graph.get_roots();
      //  L ← Empty list that will contain the sorted elements
      topological_ordering.clear();
      while(!S.empty()) { // while S is non-empty do
        
        
        switch(mode) { // apply heuristics based on mode
          case 0: {
            std::stable_sort(S.begin(), S.end(), source_comp);
            break;
          } case 1: {
            std::stable_sort(S.begin(), S.end(), source_comp);
            std::stable_sort(S.begin(), S.end(), height_comp);
            break;
          } case 2: {
            std::stable_sort(S.begin(), S.end(), source_comp);
            std::stable_sort(S.begin(), S.end(), crit_comp);
            break;
          } case 3: {
            std::stable_sort(S.begin(), S.end(), source_comp);
            std::stable_sort(S.begin(), S.end(), fan_comp);
            break;
          } case 4: {
            std::stable_sort(S.begin(), S.end(), source_comp);
            std::stable_sort(S.begin(), S.end(), crit_comp);
            std::stable_sort(S.begin(), S.end(), height_comp);
            break;
          } case 5: {
            std::stable_sort(S.begin(), S.end(), source_comp);
            std::stable_sort(S.begin(), S.end(), fan_comp);
            std::stable_sort(S.begin(), S.end(), height_comp);
            break;
          } // remove a node n from S
        }
        dependence_node* n = S.front(); // source is default heuristic if nothing else works
        S.erase(S.begin()); 


        dependence_node* not_copied_n = sequence[n->get_instruction_number()];
        topological_ordering.push_back(not_copied_n); // add n to tail of L

        std::vector<dependence_node* > dependents = n->get_dependents();
        for(dependence_node* m : dependents) { // for each node m with an edge e from n to m do
          m->remove_parent(n); // remove edge e from the graph
          if (m->get_parents().empty()) { // if m has no other incoming edges then
            S.push_back(m); // insert m into S
          }
        }
      }
    }

    // Author: Benjamin Greenwood
    std::vector<std::string> listScheduling() {

      topological_sort(mode);
      std::vector<std::string> schedule;

      for(auto n : topological_ordering) {

        std::vector<dependence_node* > parents = n->get_parents();
        int earliest_possible = 0; // analogous to s in the text book's algorithm

        // s = max(S(p) + d_e)
        for (auto p : parents) {
          if (p->is_scheduled()) {
            int dependent_cycle = p->get_cycle_scheduled() + p->get_latency();

            // we can issue WAR dependencies in the same cycle since reads happen early and writes commit at the end
            std::vector<std::string> p_reads = p->get_read_locations();
            std::string p_write = p->get_write_location();
            std::vector<std::string> n_reads = n->get_read_locations();
            std::string n_write = n->get_write_location();

            if(n_write.compare("mem") != 0) { // memory WARs not handled this way
              for(std::string p_read : p_reads) {
                if(p_read.compare(n_write) == 0) { // there is a WAR dependency

                  // but we still have to make sure there's no RAW or WAW dependency
                  if(p_write.compare(n_write) != 0) { // no WAW
                    bool no_RAW = true;
                    for(std::string n_read : n_reads) {
                      if(n_read.compare(p_write) == 0) {
                        no_RAW = false;
                      }
                    }
                    // there's no WAW, no RAW, and there is a WAR
                    if(no_RAW) {
                      dependent_cycle = p->get_cycle_scheduled();
                    }
                  }
                }
              } 
            }

            if(dependent_cycle > earliest_possible)
              earliest_possible = dependent_cycle;
            
          } else {
            std::cout << "THIS SHOULD NEVER HAPPEN\n";
          }
        }

        while(!(n->is_scheduled())) {
          // add more reservation table entries if needed
          if(earliest_possible == global_rt.size())
            add_cycle();

          std::unordered_map<std::string, int> instruction_rt = n->get_rt();

          // see if there are enough op units
          bool resource_needs_satisfied = true;
          for(const auto& resource_need : instruction_rt)
            if(global_rt[earliest_possible][resource_need.first] - resource_need.second < 0)
              resource_needs_satisfied = false;
          
          // if needs are satisfied, schedule it
          if(resource_needs_satisfied && global_rt[earliest_possible]["OPERATIONS"] > 0) {

            // however, we can't issue stw for two cycles after a ldw...
            bool no_store_load_overlap = true;
            if(n->get_operation().compare("stw") == 0) {
              if(earliest_possible > 0 && strContains(schedule[earliest_possible - 1], " ldw ")) {
                no_store_load_overlap = false;
              } else if(earliest_possible > 1 && strContains(schedule[earliest_possible - 2], " ldw ")) {
                no_store_load_overlap = false;
              }
            }

            if(no_store_load_overlap) { // SCHEDULE IT!
              n->set_cycle_scheduled(earliest_possible);

              while (schedule.size() <= earliest_possible)
                schedule.push_back("");
              
              schedule[earliest_possible].append(instructions[n->get_original_index()] + "\n");

              // update reservation tables
              for(const auto& resource_need : instruction_rt)
                global_rt[earliest_possible][resource_need.first] = global_rt[earliest_possible][resource_need.first] - resource_need.second;
              global_rt[earliest_possible]["OPERATIONS"] = global_rt[earliest_possible]["OPERATIONS"] - 1;
              
              break;
            }
          }

          earliest_possible++;
        }

        // make sure reservation table is big enough
        int latency = n->get_latency();
        while(global_rt.size() <= earliest_possible + latency) {
          add_cycle();
        }
      }

      // add ;; delimiters
      for(int i = 0; i < schedule.size(); i++) {
        schedule[i] += ";;\n";
      }

      return schedule;
    }

};

// Author: Jim Tsai
int check_height2(dependence_node* node, std::vector<dependence_node* >top_order){
  std::unordered_map<int, int> height;
  int max_height=0;
  for(auto t : top_order){
    height[t->get_instruction_number()] = -1;
  }
  dependence_node* concern_node;
  std::vector<dependence_node* >dependences;
  concern_node = node;
  // std::cout << concern_node->get_instruction_number() << "\n";
  height[concern_node->get_instruction_number()] = concern_node->get_latency();
  for(auto t : top_order){
    
    if(height[t->get_instruction_number()] != -1){
      // std::cout << "got a node need to update: " << t->get_instruction_number() << "\n";
      // t->print();
      dependences = t->get_dependents();
      for(auto d : dependences){
        if(height[t->get_instruction_number()] + d->get_latency() > height[d->get_instruction_number()]){
          // std::cout << "height of " << d->get_instruction_number() << " updated to " << height[t->get_instruction_number()] + d->get_latency() << " from " << height[t->get_instruction_number()] << "\n";
          // d->print();
          height[d->get_instruction_number()] = height[t->get_instruction_number()] + d->get_latency();
        }
      }

      // std::cout << "height:\n";
      // for ( auto it = height.begin(); it != height.end(); ++it )
      //   std::cout << it->first << ":" << it->second << " ";      
      // std::cout << "\n";
    }
  }

  // std::cout << "height:\n";
  for ( auto it = height.begin(); it != height.end(); ++it )
    if(it->second > max_height)
      max_height = it->second;

  return max_height;
}

//Jim Tsai
void height_pass(dependence_graph& graph){
  std::vector<dependence_node*> height_pass_nodes;
  height_pass_nodes = graph.get_topological_ordering();
  for(auto n : height_pass_nodes){
    n->set_height_jim(check_height2(n, height_pass_nodes));
    // std::cout << n->get_instruction_number() << "\n";
  }
  // check_height2(height_pass_nodes[0], height_pass_nodes);
}

// Author: Jim Tsai
void criticality_pass(dependence_graph& graph){
  int alu_uses = 0;
  int mem_uses = 0;
  int mul_uses = 0;
  float alu_ratio, mem_ratio, mul_ratio;
  std::vector<dependence_node*> crit_pass_nodes;
  crit_pass_nodes = graph.get_sequence();
  
  for(auto n : crit_pass_nodes){
    std::unordered_map<std::string, int> rt;
    rt = n->get_rt();
    if(rt["ALU"]){
      alu_uses++;
    }

    if(rt["MUL"]){
      mul_uses++;
    }

    if(rt["MEM"]){
      mem_uses++;
    }
  }

  // std::cout << "alu "<< alu_uses << "\n";

  alu_ratio = float(alu_uses) / 4.0;
  mem_ratio = float(mem_uses) / 1.0;
  mul_ratio = float(mul_uses) / 2.0;

  for(auto n : crit_pass_nodes){
    std::unordered_map<std::string, int> rt;
    rt = n->get_rt();
    if(rt["ALU"]){
      n->set_criticality(alu_ratio);
    }

    if(rt["MUL"]){
      n->set_criticality(mul_ratio);
    }

    if(rt["MEM"]){
      n->set_criticality(mem_ratio);
    }
  }
}






/*
Inputs:
    - std::vector<std::string> instructions. The input is a vector of strings. Each
      string in the vector is an instruction in the original vex code.
    - <int> mode: value indicating which heuristic ordering to use

Returns : std::vector<std::string>

The function should return a vector of strings. Each string should be a scheduled instruction or ;; that marks the end of a VLIW instruction word.
*/
std::vector<std::string>  scheduleVLIW(std::vector<std::string> instructions,
                                       int mode)
{
    std::vector<std::string> scheduledVLIW;

    /* TODO : Implement your code here */
    // prepend the first call instruction, do not schedule it
    if (strContains(instructions[0], "call")) {
      prologue.push_back(instructions[0]);
      prologue.push_back(";;");
      instructions.erase(instructions.begin());
    }

    dependence_graph dg(instructions, mode);
    dg.rename_registers();

    dg.topological_sort(mode);
    height_pass(dg); //Jim Tsai

    // std::vector<dependence_node* > node_seq;
    // node_seq = dg.get_sequence();
    //   for(auto n : node_seq){
    //     std::cout << n->get_height() << "\n";
    //   }


    // std::vector<dependence_node* > roots = dg.get_sequence();
    // for(dependence_node* root : roots) {
    //   std::cout << "instruction " << root->get_instruction_number();
    //   std::cout << "\nben height: " << root->get_height();
    //   std::cout << "\njim height: " << root->get_height_jim() << "\n";
    // }


    criticality_pass(dg); // Jim Tsai
    
    // node_seq = dg.get_sequence();
    // for(auto n : node_seq){
    //   std::vector<dependence_node* > dependence;
    //   dependence = n->get_dependents();
    //   std::cout << "dependents for " << n->get_instruction_number() << "\n";
    //   for(auto d : dependence) {
    //     std::cout << d->get_instruction_number() << "\n";
    //   }
    //   std::cout << "\n";
    // }

    // prologue.push_back("STARTING SCHEDULE:\n");
    // epilogue[0] = "ENDING SCHEDULE\n"; // just for human-reading the schedule produced

    scheduledVLIW = dg.listScheduling();

    return scheduledVLIW;
}

int main(int argc, char *argv[])
{

   if(argc != 4) {
       std::cout << "Invalid parameters \n";
       std::cout << "Expected use ./vliwSpeculation ";
       std::cout << "<input file name> <output file name> <mode>\n";
   }
 
   const char* inputFile = argv[1];
   const char* vliwSpeculationOutput = argv[2];
   int mode = atoi(argv[3]);

   std::vector<std::string> instructions;
   std::vector<std::string> scheduledVLIW;
 
   /* Read instructions from the file */
   instructions = readInput(inputFile);

   /* Schedule instructions */
   scheduledVLIW = scheduleVLIW(instructions, mode);

   /* Print scheduled instructions to file */
   printOutput(vliwSpeculationOutput, scheduledVLIW);
}