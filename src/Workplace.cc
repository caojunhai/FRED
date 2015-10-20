/*
  This file is part of the FRED system.

  Copyright (c) 2010-2015, University of Pittsburgh, John Grefenstette,
  Shawn Brown, Roni Rosenfield, Alona Fyshe, David Galloway, Nathan
  Stone, Jay DePasse, Anuroop Sriram, and Donald Burke.

  Licensed under the BSD 3-Clause license.  See the file "LICENSE" for
  more information.
*/

//
//
// File: Workplace.cc
//
#include <climits>

#include "Workplace.h"
#include "Global.h"
#include "Params.h"
#include "Random.h"
#include "Person.h"
#include "Disease.h"
#include "Disease_List.h"
#include "Place_List.h"
#include "Population.h"
#include "Office.h"
#include "Utils.h"

//Private static variables that will be set by parameter lookups
double Workplace::contacts_per_day;
double** Workplace::prob_transmission_per_contact;
int Workplace::Office_size = 0;
int Workplace::Small_workplace_size = 0;
int Workplace::Medium_workplace_size = 0;
int Workplace::Large_workplace_size = 0;

//Private static variables for population level statistics
int Workplace::workers_in_small_workplaces = 0;
int Workplace::workers_in_medium_workplaces = 0;
int Workplace::workers_in_large_workplaces = 0;
int Workplace::workers_in_xlarge_workplaces = 0;
int Workplace::total_workers = 0;

vector<int> Workplace::workplace_size_max;
vector<int> Workplace::workers_by_workplace_size;
int Workplace::workplace_size_group_count = 0;

Workplace::Workplace() : Place() {
  this->type = WORKPLACE;
  this->subtype = fred::PLACE_SUBTYPE_NONE;
  this->intimacy = 0.01;
  this->offices.clear();
  this->next_office = 0;
}

Workplace::Workplace(const char *lab, fred::place_subtype _subtype, fred::geo lon, fred::geo lat) : Place(lab, lon, lat) {
  this->type = WORKPLACE;
  this->subtype = _subtype;
  this->intimacy = 0.01;
  this->offices.clear();
  this->next_office = 0;
}

void Workplace::get_parameters() {
  // people per office
  Params::get_param_from_string("office_size", &Workplace::Office_size);

  // workplace size limits
  Params::get_param_from_string("small_workplace_size", &Workplace::Small_workplace_size);
  Params::get_param_from_string("medium_workplace_size", &Workplace::Medium_workplace_size);
  Params::get_param_from_string("large_workplace_size", &Workplace::Large_workplace_size);
    
  Workplace::workplace_size_group_count = Params::get_param_vector((char*)"workplace_size_max", Workplace::workplace_size_max);
  //Add the last column so that it goes to intmax
  Workplace::workplace_size_max.push_back(INT_MAX);
  Workplace::workplace_size_group_count++;
  //Set all of the workplace counts to 0
  for(int i = 0; i < Workplace::workplace_size_group_count; ++i) {
    Workplace::workers_by_workplace_size.push_back(0);
  }

  Params::get_param_from_string("workplace_contacts", &Workplace::contacts_per_day);
  int n = Params::get_param_matrix((char *)"workplace_trans_per_contact", &Workplace::prob_transmission_per_contact);
  if(Global::Verbose > 1) {
    printf("\nWorkplace_contact_prob:\n");
    for(int i  = 0; i < n; ++i) {
      for(int j  = 0; j < n; ++j) {
	      printf("%f ", Workplace::prob_transmission_per_contact[i][j]);
      }
      printf("\n");
    }
  }

  // normalize contact parameters
  // find max contact prob
  double max_prob = 0.0;
  for(int i  = 0; i < n; ++i)  {
    for(int j  = 0; j < n; ++j) {
      if(Workplace::prob_transmission_per_contact[i][j] > max_prob) {
	      max_prob = Workplace::prob_transmission_per_contact[i][j];
      }
    }
  }

  // convert max contact prob to 1.0
  if(max_prob > 0) {
    for(int i  = 0; i < n; ++i)  {
      for(int j  = 0; j < n; ++j) {
	      Workplace::prob_transmission_per_contact[i][j] /= max_prob;
      }
    }
    // compensate contact rate
    Workplace::contacts_per_day *= max_prob;
  }

  if(Global::Verbose > 0) {
    printf("\nWorkplace_contact_prob after normalization:\n");
    for(int i  = 0; i < n; ++i)  {
      for(int j  = 0; j < n; ++j) {
	      printf("%f ", Workplace::prob_transmission_per_contact[i][j]);
      }
      printf("\n");
    }
    printf("\ncontact rate: %f\n", Workplace::contacts_per_day);
  }
  // end normalization
}

// this method is called after all workers are assigned to workplaces
void Workplace::prepare() {

  assert(Global::Pop.is_load_completed());
  // update employment stats based on size of workplace
  if(get_size() < Workplace::Small_workplace_size) {
    Workplace::workers_in_small_workplaces += get_size();
  } else if(get_size() < Workplace::Medium_workplace_size) {
    Workplace::workers_in_medium_workplaces += get_size();
  } else if(get_size() < Workplace::Large_workplace_size) {
    Workplace::workers_in_large_workplaces += get_size();
  } else {
    Workplace::workers_in_xlarge_workplaces += get_size();
  }
  Workplace::total_workers += get_size();
  
    for(int i = 0; i < Workplace::workplace_size_group_count; ++i) {
    if(get_size() < Workplace::workplace_size_max[i]) {
      Workplace::workers_by_workplace_size[i] += get_size();
      break;
    }
  }

//  printf("DEBUG: Workplace[%d] ... size[%d]\n", this->get_id(), this->get_size());
//  int wp_size_min = 0;
//  for(int i = 0; i < Workplace::workplace_size_group_count; ++i) {
//    printf("DEBUG: workplace size of %d - %d = %d\n", wp_size_min, Workplace::workplace_size_max[i], Workplace::workers_by_workplace_size[i]);
//    wp_size_min = Workplace::workplace_size_max[i] + 1;
//  }
//  printf("DEBUG: -------------------------------------------------\n\n");

  // now call base class function to perform preparations common to all Places 
  Place::prepare();
}

double Workplace::get_transmission_prob(int disease, Person* i, Person* s) {
  // i = infected agent
  // s = susceptible agent
  int row = get_group(disease, i);
  int col = get_group(disease, s);
  double tr_pr = Workplace::prob_transmission_per_contact[row][col];
  return tr_pr;
}

double Workplace::get_contacts_per_day(int disease) {
  return Workplace::contacts_per_day;
}

int Workplace::get_number_of_rooms() {
  if(Workplace::Office_size == 0) {
    return 0;
  }
  int rooms = get_size() / Workplace::Office_size;
  this->next_office = 0;
  if(get_size() % Workplace::Office_size) {
    rooms++;
  }
  if(rooms == 0) {
    ++rooms;
  }
  return rooms;
}

void Workplace::setup_offices(Allocator<Office> &office_allocator) {
  int rooms = get_number_of_rooms();

  FRED_STATUS(1, "workplace %d %s number %d rooms %d\n", id, label, get_size(), rooms );
  
  for(int i = 0; i < rooms; ++i) {
    char new_label[128];
    sprintf(new_label, "%s-%03d", this->get_label(), i);
    
    Office* office = new(office_allocator.get_free())Office(new_label,
							           fred::PLACE_SUBTYPE_NONE,
							           this->get_longitude(),
							           this->get_latitude());

    office->set_workplace(this);

    this->offices.push_back(office);

    FRED_STATUS(1, "workplace %d %s added office %d %s %d\n",
		id, label,i,office->get_label(),office->get_id());
  }
}

Place* Workplace::assign_office(Person* per) {

  if(Workplace::Office_size == 0) {
    return NULL;
  }

  FRED_STATUS( 1, "assign office for person %d at workplace %d %s size %d == ",
	       per->get_id(), id, label, get_size());

  // pick next office, round-robin
  int i = this->next_office;

  assert(this->offices.size() > i);

  FRED_STATUS(1, "office = %d %s %d\n",
	      i, offices[i]->get_label(), offices[i]->get_id());

  // update next pick
  if(this->next_office < (int)this->offices.size() - 1) {
    this->next_office++;
  } else {
    this->next_office = 0;
  }
  return this->offices[i];
}
