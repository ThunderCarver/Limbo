#ifndef LIMBO_ALGORITHMS_COLORING_LP
#define LIMBO_ALGORITHMS_COLORING_LP

#include <iostream>
#include <vector>
#include <queue>
#include <set>
#include <cassert>
#include <cmath>
#include <stdlib.h>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <boost/cstdint.hpp>
#include <boost/graph/graph_concepts.hpp>
#include <boost/graph/subgraph.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/graph/bipartite.hpp>
//#include <boost/graph/kruskal_min_spanning_tree.hpp>
#include <boost/graph/prim_minimum_spanning_tree.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <limbo/string/String.h>
#include <limbo/algorithms/coloring/Coloring.h>
#include "gurobi_c++.h"

namespace limbo { namespace algorithms { namespace coloring {

using std::vector;
using std::queue;
using std::set;
using std::cout;
using std::endl;
using std::string;

template <typename GraphType>
class LPColoring : public Coloring<GraphType>
{
	public:
        typedef Coloring<GraphType> base_type;
		typedef typename base_type::graph_type graph_type;
		typedef typename base_type::graph_vertex_type graph_vertex_type;
		typedef typename base_type::graph_edge_type graph_edge_type;
		typedef typename base_type::vertex_iterator_type vertex_iterator_type;
		typedef typename base_type::edge_iterator_type edge_iterator_type;
        typedef typename base_type::EdgeHashType edge_hash_type;

        struct NonIntegerInfo
        {
            uint32_t vertex_non_integer_num;
            uint32_t edge_non_integer_num;
            uint32_t vertex_half_integer_num;
            uint32_t edge_half_integer_num;

            NonIntegerInfo() 
                : vertex_non_integer_num(std::numeric_limits<uint32_t>::max())
                , edge_non_integer_num(std::numeric_limits<uint32_t>::max())
                , vertex_half_integer_num(std::numeric_limits<uint32_t>::max())
                , edge_half_integer_num(std::numeric_limits<uint32_t>::max())
            {}
        };
        /// information for a variable of a constraint 
        struct ConstrVariableInfo
        {
            double coeff; 
            char sense; ///< '>' or '<'

            ConstrVariableInfo() 
                : coeff(0.0)
                , sense('>')
            {}
            void set(double c, char s)
            {
                coeff = c;
                sense = s;
            }
            bool same_direction(ConstrVariableInfo const& rhs) const 
            {
                if (coeff == 0.0 || rhs.coeff == 0.0)
                    return true;
                else if (sense == rhs.sense)
                    return (coeff > 0 && rhs.coeff > 0) || (coeff < 0 && rhs.coeff < 0);
                else 
                    return (coeff > 0 && rhs.coeff < 0) || (coeff < 0 && rhs.coeff > 0);
            }
        };

		/// member functions
		/// constructor
		LPColoring(graph_type const& g);
		/// destructor
		~LPColoring() {};

	protected:
		/// \return objective value 
		/// relaxed linear programming based coloring for the conflict graph (this->m_graph)
		double coloring(); 

		/// apply coloring solution 
		void apply_solution(vector<GRBVar> const& vColorBits);

		/// create the NoStitchGraph (this->m_graph) from the (m_conflict_graph)
		void initialize();
        /// create basic variables, objective and constraints
        void set_optimize_model(vector<GRBVar>& vColorBits, vector<GRBVar>& vEdgeBits, GRBLinExpr& obj, GRBModel& optModel);
        /// set anchor vertex 
        void set_anchor(vector<GRBVar>& vColorBits) const; 
        /// for each color bit pair of a vertex 
        void adjust_variable_pair_in_objective(vector<GRBVar> const& vColorBits, GRBLinExpr& obj) const; 
        /// for each color bit pair of vertices of an edge 
        void adjust_conflict_edge_vertices_in_objective(vector<GRBVar> const& vColorBits, GRBLinExpr& obj) const;
        /// odd cycle constraints from Prof. Baldick
        void add_odd_cycle_constraints(vector<GRBVar> const& vColorBits, GRBModel& optModel); 

		/// DFS to search for the odd cycles, stored in m_odd_cycles
		void get_odd_cycles(graph_vertex_type const& v, vector<vector<graph_vertex_type> >& vOddCyle);
        /// \return the vertex with the largest degree
		graph_vertex_type max_degree_vertex() const;

		/// Optimal rounding based on binding constraints
		void rounding_with_binding_analysis(GRBModel& optModel, vector<GRBVar>& vColorBits, vector<GRBVar>& vEdgeBits) const;

		/// greedy final coloring refinement
		uint32_t post_refinement();
        ///  refine coloring solution of an edge 
		bool refine_color(graph_edge_type const& e);

        /// compute number of non-integers and half-integers for both vertex variables and edge variables 
        void non_integer_num(vector<GRBVar> const& vColorBits, vector<GRBVar> const& vEdgeBits, NonIntegerInfo& info) const;
        /// compute number of non-integers and half-integers with given variable array 
        void non_integer_num(vector<GRBVar> const& vVariables, uint32_t& nonIntegerNum, uint32_t& halfIntegerNum) const;

		/// check if a variable is integer or not 
		bool is_integer(double value) const {return value == floor(value);}

		/// members
		uint32_t m_constrs_num;
};

/// constructor
template <typename GraphType>
LPColoring<GraphType>::LPColoring(graph_type const& g) 
    : LPColoring<GraphType>::base_type(g)
    , m_constrs_num(0)
{
}

//DFS to search for the odd cycles, stored in m_odd_cycles
template <typename GraphType>
void LPColoring<GraphType>::get_odd_cycles(graph_vertex_type const& v, vector<vector<LPColoring<GraphType>::graph_vertex_type> >& vOddCyle) 
{
	//odd_cycle results
	vOddCyle.clear();

	//the array denoting the distance/visiting of the graph 
	uint32_t numVertices = boost::num_vertices(this->m_graph);
	vector<int32_t> vNodeDistColor (numVertices, -1);
	vector<bool> vNodeVisited (numVertices, false);

	//inCycle flag
	vector<bool> vInCycle (numVertices, false);

	// stack for DFS
    // use std::vector instead of std::stack for better memory control
	vector<graph_vertex_type> vVertexStack;
	vVertexStack.reserve(numVertices);
	vNodeVisited[v] = true;
	vNodeDistColor[v] = 0;
	vVertexStack.push_back(v);
	while (!vVertexStack.empty()) 
	{
		//determine whether the top element needs to be popped
		bool popFlag = true;
		//access the top element on the dfs_stack
        // current vertex 
		graph_vertex_type cv = vVertexStack.back();
		//access the neighbors 
		typename boost::graph_traits<graph_type>::adjacency_iterator ui, uie;
		for(boost::tie(ui, uie) = adjacent_vertices(cv, this->m_graph); ui != uie; ++ui) 
		{
            graph_vertex_type u = *ui;

			if(vNodeDistColor[u] == -1) 
			{
				vNodeDistColor[u] = 1 - vNodeDistColor[cv];
				vNodeDistColor[u] = true;
				//push to the stack
				vVertexStack.push_back(u);
				popFlag = false;
				break;
			}
		} //end for 

		if (true == popFlag) 
		{
			//detect the odd cycle
            for (boost::tie(ui, uie) = adjacent_vertices(cv, this->m_graph); ui != uie; ++ui)
			{
                graph_vertex_type u = *ui;
				if (vNodeVisited[u] && (vNodeDistColor[u] == vNodeDistColor[cv])) 
				//if (*vi == v && (nodeDist[next_index] == nodeDist[curr_index])) // we only care about odd cycle related to v 
				{
					//suppose v is not in the current cycle 
					vInCycle[v] = true;
					//detect the cycle between curr_v and *vi
					vector<graph_vertex_type> cycle;
					int32_t cnt = vVertexStack.size();
					for(int32_t k = cnt - 1; k >= 0; k--) 
					{
						cycle.push_back(vVertexStack[k]);
						//flag the nodes in cycle
						vInCycle[vVertexStack[k]] = true;
						if(vVertexStack[k] == u) break;
					}
					//store the cycle, when v is in cycle
					if(!cycle.empty() && vInCycle[v]) 
					{
                        vOddCyle.push_back(vector<graph_vertex_type>(0));
                        vOddCyle.back().swap(cycle);
					} 
				}//end if
			}//end for vi

			//pop the top element
			vVertexStack.pop_back();
            vNodeVisited[cv] = false;
		}//end if popFlag
	}//end while
}

// the vertex with the largest degree
template <typename GraphType>
typename LPColoring<GraphType>::graph_vertex_type LPColoring<GraphType>::max_degree_vertex() const
{
    graph_vertex_type u = 0;
    uint32_t maxDegree = 0;
    vertex_iterator_type vi, vie;
    for (boost::tie(vi, vie) = boost::vertices(this->m_graph); vi != vie; ++vi)
    {
        uint32_t d = boost::degree(*vi, this->m_graph);
        if (d > maxDegree)
        {
            u = *vi;
            maxDegree = d;
        }
    }
    return u;
}

template <typename GraphType>
void LPColoring<GraphType>::set_optimize_model(vector<GRBVar>& vColorBits, vector<GRBVar>& vEdgeBits, GRBLinExpr& obj, GRBModel& optModel)
{
	uint32_t numVertices = boost::num_vertices(this->m_graph);
	uint32_t numEdges = boost::num_edges(this->m_graph);
    uint32_t numColorBits = numVertices<<1;

	//set up the LP variables
	vColorBits.reserve(numColorBits);
	vEdgeBits.reserve(numEdges);
	// vertex and edge variables 
    char buf[64];
	for (uint32_t i = 0; i != numColorBits; ++i)
	{
        sprintf(buf, "v%u", i);
		vColorBits.push_back(optModel.addVar(0.0, 1.0, 0.0, GRB_CONTINUOUS, buf));
	}
	for (uint32_t i = 0; i != numEdges; ++i)
	{
		// some variables here may not be used 
        sprintf(buf, "e%u", i);
		vEdgeBits.push_back(optModel.addVar(0.0, 1.0, 0.0, GRB_CONTINUOUS, buf));
	}
	//Integrate new variables
	optModel.update();

    // set up objective
    obj.clear(); // set to 0 
    optModel.setObjective(obj, GRB_MINIMIZE);

	//set up the LP constraints
    edge_iterator_type ei, eie; 
	for(boost::tie(ei, eie) = boost::edges(this->m_graph); ei != eie; ++ei) 
	{
        graph_edge_type e = *ei;
        graph_vertex_type s = boost::source(e, this->m_graph);
        graph_vertex_type t = boost::target(e, this->m_graph);

        uint32_t bitIdxS = s<<1;
        uint32_t bitIdxT = t<<1;

        int32_t w = this->edge_weight(e);
        assert_msg(w > 0, "no stitch edge allowed, positive edge weight expected: " << w);

        sprintf(buf, "R%u", m_constrs_num++);  
        optModel.addConstr(
                vColorBits[bitIdxS] + vColorBits[bitIdxS+1] 
                + vColorBits[bitIdxT] + vColorBits[bitIdxT+1] >= 1
                , buf);

        sprintf(buf, "R%u", m_constrs_num++);  
        optModel.addConstr(
                1 - vColorBits[bitIdxS] + vColorBits[bitIdxS+1] 
                + 1 - vColorBits[bitIdxT] + vColorBits[bitIdxT+1] >= 1
                , buf);

        sprintf(buf, "R%u", m_constrs_num++);  
        optModel.addConstr(
                vColorBits[bitIdxS] + 1 - vColorBits[bitIdxS+1] 
                + vColorBits[bitIdxT] + 1 - vColorBits[bitIdxT+1] >= 1
                , buf);

        sprintf(buf, "R%u", m_constrs_num++);  
        optModel.addConstr(
                1 - vColorBits[bitIdxS] + 1 - vColorBits[bitIdxS+1] 
                + 1 - vColorBits[bitIdxT] + 1 - vColorBits[bitIdxT+1] >= 1
                , buf);
	}

	if (this->color_num() == base_type::THREE)
	{
		for(uint32_t k = 0; k < numColorBits; k += 2) 
		{
			sprintf(buf, "R%u", m_constrs_num++);  
			optModel.addConstr(
                    vColorBits[k] + vColorBits[k+1] <= 1, 
                    buf);
		}
	}

	//Integrate new variables
	optModel.update();
}

template <typename GraphType>
void LPColoring<GraphType>::set_anchor(vector<GRBVar>& vColorBits) const 
{
    if (this->has_precolored()) // no anchor if containing precolored vertices 
        return;
	//Anchoring the coloring of the vertex with largest degree
	graph_vertex_type anchorVertex = max_degree_vertex();
	uint32_t bitIdxAnchor = anchorVertex<<1;
	vColorBits[bitIdxAnchor].set(GRB_DoubleAttr_UB, 0.0);
	vColorBits[bitIdxAnchor].set(GRB_DoubleAttr_LB, 0.0);
	vColorBits[bitIdxAnchor+1].set(GRB_DoubleAttr_UB, 0.0);
	vColorBits[bitIdxAnchor+1].set(GRB_DoubleAttr_LB, 0.0);
}

/// tune objective for each color bit pair of vertex  
template <typename GraphType>
void LPColoring<GraphType>::adjust_variable_pair_in_objective(vector<GRBVar> const& vColorBits, GRBLinExpr& obj) const 
{
    for(uint32_t k = 0, ke = vColorBits.size(); k < ke; k += 2) 
    {
        double value1 = vColorBits[k].get(GRB_DoubleAttr_X);
        double value2 = vColorBits[k+1].get(GRB_DoubleAttr_X);
        if (!is_integer(value1) || !is_integer(value2))
        {
            if (value1 > value2)
                obj += vColorBits[k+1]-vColorBits[k];
            else if (value1 < value2)
                obj += vColorBits[k]-vColorBits[k+1];
        }
    }//end for 
}

/// tune objective for each color bit pair along conflict edges 
template <typename GraphType>
void LPColoring<GraphType>::adjust_conflict_edge_vertices_in_objective(vector<GRBVar> const& vColorBits, GRBLinExpr& obj) const 
{
    edge_iterator_type ei, eie; 
	for(boost::tie(ei, eie) = boost::edges(this->m_graph); ei != eie; ++ei) 
    {
        graph_edge_type e = *ei;
        graph_vertex_type s = boost::source(e, this->m_graph);
        graph_vertex_type t = boost::target(e, this->m_graph);

        for (uint32_t i = 0; i != 2; ++i)
        {
            uint32_t bitIdxS = (s<<1)+i;
            uint32_t bitIdxT = (t<<1)+i;

            double value1 = vColorBits[bitIdxS].get(GRB_DoubleAttr_X);
            double value2 = vColorBits[bitIdxT].get(GRB_DoubleAttr_X);

            if (value1 > value2)
                obj += vColorBits[bitIdxT]-vColorBits[bitIdxS]; // reverse, as we minimize objective
            else if (value1 < value2)
                obj += vColorBits[bitIdxS]-vColorBits[bitIdxT]; // reverse, as we minimize objective
        }
    }//end for 
}

/// odd cycle trick from Prof. Baldick
template <typename GraphType>
void LPColoring<GraphType>::add_odd_cycle_constraints(vector<GRBVar> const& vColorBits, GRBModel& optModel) 
{
    char buf[64];
    vector<vector<graph_vertex_type> > vOddCyle;
    for(uint32_t k = 0, ke = vColorBits.size(); k < ke; k += 2) 
    {
        graph_vertex_type v = k>>1;
        //this->odd_cycle_mst(curr_v);
        this->get_odd_cycles(v, vOddCyle);

        for (typename vector<vector<graph_vertex_type> >::const_iterator it1 = vOddCyle.begin(), it1e = vOddCyle.end(); it1 != it1e; ++it1)
        {
            vector<graph_vertex_type> const& oddCycle = *it1;
            int32_t cycleLength = oddCycle.size(); // safer to use integer as we do minus afterward
            GRBLinExpr constraint1 = 0;
            GRBLinExpr constraint2 = 0;

            for (typename vector<graph_vertex_type>::const_iterator it2 = oddCycle.begin(), it2e = oddCycle.end(); it2 != it2e; ++it2)
            {
                graph_vertex_type u = *it2;
                constraint1 += vColorBits[u<<1];
                constraint2 += vColorBits[(u<<1)+1];
            }

            sprintf(buf, "ODD%lu_%u", v, m_constrs_num++);
            optModel.addConstr(constraint1 >= 1, buf);
            sprintf(buf, "ODD%lu_%u", v, m_constrs_num++);
            optModel.addConstr(constraint1 <= cycleLength-1, buf);
            sprintf(buf, "ODD%lu_%u", v, m_constrs_num++);
            optModel.addConstr(constraint2 >= 1, buf);
            sprintf(buf, "ODD%lu_%u", v, m_constrs_num++);
            optModel.addConstr(constraint2 <= cycleLength-1, buf);
        }
    }//end for k
}

//relaxed linear programming based coloring for the conflict graph (this->m_graph)
template <typename GraphType> 
double LPColoring<GraphType>::coloring() 
{
#ifdef DEBUG_LPCOLORING
    this->write_graph("initial_input");
#endif
    vector<GRBVar> vColorBits;
    vector<GRBVar> vEdgeBits;
    GRBLinExpr obj;

	//set up the LP environment
	GRBEnv env;
	//mute the log from the LP solver
	//env.set(GRB_IntParam_OutputFlag, 0);
    // set number of threads 
	if (this->m_threads > 0 && this->m_threads < std::numeric_limits<int32_t>::max())
		env.set(GRB_IntParam_Threads, this->m_threads);
    // default algorithm 
    env.set(GRB_IntParam_Method, -1);
    // since GRBModel does not allow default constructor, we have to setup GRBEnv before it
	GRBModel optModel = GRBModel(env);

    // initialize model and set anchor vertex 
    set_optimize_model(vColorBits, vEdgeBits, obj, optModel);
    set_anchor(vColorBits);

	optModel.optimize();
	int optStatus = optModel.get(GRB_IntAttr_Status);
    assert_msg(optStatus != GRB_INFEASIBLE, "model is infeasible");

    NonIntegerInfo prevInfo; // initialize to numeric max 
    NonIntegerInfo curInfo;
    non_integer_num(vColorBits, vEdgeBits, curInfo);
	//iteratively scheme
	while(curInfo.vertex_non_integer_num > 0 && curInfo.vertex_non_integer_num < prevInfo.vertex_non_integer_num) 
	{
		//set the new objective
		//push the non-half_integer to 0/1
		// tune objective for a pair of values 
        adjust_variable_pair_in_objective(vColorBits, obj);
		// tune objective for a pair of value along conflict edges 
        adjust_conflict_edge_vertices_in_objective(vColorBits, obj);

		optModel.setObjective(obj);

		//add new constraints
		//odd cycle trick from Prof. Baldick
        add_odd_cycle_constraints(vColorBits, optModel);

		//optimize the new model
		optModel.update();
		optModel.optimize();

        optStatus = optModel.get(GRB_IntAttr_Status);
        assert_msg(optStatus != GRB_INFEASIBLE, "model is infeasible");

        prevInfo = curInfo;
        non_integer_num(vColorBits, vEdgeBits, curInfo);
	}//end while

	// binding analysis
    rounding_with_binding_analysis(optModel, vColorBits, vEdgeBits);
    // apply coloring solution 
    apply_solution(vColorBits);
    // post refinement 
	post_refinement();

#ifdef DEBUG_LPCOLORING
    this->write_graph("final_output");
#endif
    return this->calc_cost(this->m_vColor);
}

template <typename GraphType>
void LPColoring<GraphType>::apply_solution(vector<GRBVar> const& vColorBits)
{
    for (uint32_t i = 0, ie = this->m_vColor.size(); i != ie; ++i)
    {
        GRBVar const& var1 = vColorBits[i<<1];
        GRBVar const& var2 = vColorBits[(i<<1)+1];
        int8_t b1 = round(var1.get(GRB_DoubleAttr_X));
        int8_t b2 = round(var2.get(GRB_DoubleAttr_X));
        this->m_vColor[i] = (b1<<1)+b2;
    }
}

/// optimal rounding based on the binding analysis
/// but only a part of all vertices can be rounded 
template <typename GraphType>
void LPColoring<GraphType>::rounding_with_binding_analysis(GRBModel& optModel, vector<GRBVar>& vColorBits, vector<GRBVar>& vEdgeBits) const
{
    NonIntegerInfo prevInfo; // initialize to numeric max 
    NonIntegerInfo curInfo;
    non_integer_num(vColorBits, vEdgeBits, curInfo);
	//iteratively scheme
	while(curInfo.vertex_non_integer_num > 0 && curInfo.vertex_non_integer_num < prevInfo.vertex_non_integer_num) 
    {
        for (uint32_t i = 0, ie = vColorBits.size(); i < ie; i += 2)
        {
            GRBVar const& var1 = vColorBits[i];
            GRBVar const& var2 = vColorBits[i+1];
            double value1 = var1.get(GRB_DoubleAttr_X);
            double value2 = var2.get(GRB_DoubleAttr_X);

            if (!(value1 == 0.5 && value2 == 0.5))
                continue;

            GRBColumn column[2] = {
                optModel.getCol(var1),
                optModel.getCol(var2)
            };

            ConstrVariableInfo prevConstrInfo[2];
            ConstrVariableInfo curConstrInfo[2];

            // (0, 0), (0, 1), (1, 0), (1, 1)
            bool mValidColorBits[2][2] = {
                {true, true}, 
                {true, true}
            };
            if (this->color_num() == base_type::THREE)
                mValidColorBits[1][1] = false;

            uint32_t invalidCount = 0;
            bool failFlag = false; // whether optimal rounding is impossible 
            for (uint32_t j = 0; j != 2 && !failFlag; ++j)
                for (uint32_t k = 0, ke = column[j].size(); k != ke; ++k)
                {
                    GRBConstr constr = column[j].getConstr(k);
                    // skip non-binding constraint 
                    if (constr.get(GRB_DoubleAttr_Slack) != 0.0) 
                        continue;
                    char sense = constr.get(GRB_CharAttr_Sense);
                    curConstrInfo[0].set(optModel.getCoeff(constr, var1), sense);
                    curConstrInfo[1].set(optModel.getCoeff(constr, var2), sense);

                    // conflict sensitivity detected 
                    if (!curConstrInfo[0].same_direction(prevConstrInfo[0]) || !curConstrInfo[1].same_direction(prevConstrInfo[1]))
                    {
                        failFlag = true;
                        break;
                    }

                    // check all coloring solutions 
                    for (int32_t b1 = 0; b1 != 2; ++b1)
                        for (int32_t b2 = 0; b2 != 2; ++b2)
                        {
                            if (mValidColorBits[b1][b2])
                            {
                                double delta = curConstrInfo[0].coeff*(b1-value1) + curConstrInfo[1].coeff*(b2-value2);
                                if ((sense == '>' && delta < 0.0) || (sense == '<' && delta > 0.0)) // fail
                                {
                                    mValidColorBits[b1][b2] = false;
                                    ++invalidCount;
                                }
                            }
                        }

                    // no valid rounding solution 
                    if (invalidCount == 4)
                    {
                        failFlag = true;
                        break;
                    }

                    prevConstrInfo[0] = curConstrInfo[0];
                    prevConstrInfo[1] = curConstrInfo[1];
                }

            // apply 
            if (!failFlag)
            {
                for (int32_t b1 = 0; b1 != 2; ++b1)
                    for (int32_t b2 = 0; b2 != 2; ++b2)
                        if (mValidColorBits[b1][b2])
                        {
                            vColorBits[i].set(GRB_DoubleAttr_UB, b1);
                            vColorBits[i].set(GRB_DoubleAttr_LB, b1);
                            vColorBits[i+1].set(GRB_DoubleAttr_UB, b2);
                            vColorBits[i+1].set(GRB_DoubleAttr_LB, b2);
                        }
            }
        }

        prevInfo = curInfo;
        non_integer_num(vColorBits, vEdgeBits, curInfo);
    }
}

//post coloring refinement
template <typename GraphType>
uint32_t LPColoring<GraphType>::post_refinement() 
{
    uint32_t count = 0;
    if (!this->has_precolored()) // no post refinement if containing precolored vertices 
    {
        //greedy post refinement
        edge_iterator_type ei, eie;
        for (boost::tie(ei, eie) = boost::edges(this->m_graph); ei != eie; ++ei)
            count += refine_color(*ei);
    }

    return count;
}

/// \return true if found a coloring solution to resolve conflicts 
template <typename GraphType>
bool LPColoring<GraphType>::refine_color(LPColoring<GraphType>::graph_edge_type const& e) 
{ 
    graph_vertex_type v[2] = {
        boost::source(e, this->m_graph), 
        boost::target(e, this->m_graph)
    };

    if (this->m_vColor[v[0]] != this->m_vColor[v[1]])
        return false;

    bool mValidColors[2][4] = {
        {true, true, true, true}, 
        {true, true, true, true}
    };
    if (this->color_num() == base_type::THREE)
        mValidColors[0][3] = mValidColors[1][3] = false;

    for (int8_t c1 = 0; c1 != 4; ++c1)
        for (int8_t c2 = 0; c2 != 4; ++c2)
            if (c1 != c2 && mValidColors[c1][c2]) // no conflict allowed between v1 and v2 
            {
                typename boost::graph_traits<graph_type>::adjacency_iterator ui, uie;
                for (int32_t i = 0; i != 2; ++i)
                {
                    // cv denotes current vertex 
                    // ov denotes the other vertex 
                    graph_vertex_type cv = v[i], ov = v[!i];
                    for (boost::tie(ui, uie) = boost::adjacent_vertices(cv, this->m_graph); ui != uie; ++ui)
                    {
                        graph_vertex_type u = *ui;
                        if (u != ov)
                            mValidColors[0][this->m_vColor[u]] = false;
                    }
                }
                if (mValidColors[c1][c2]) // found coloring solution to resolve conflict 
                {
                    this->m_vColor[v[0]] = c1;
                    this->m_vColor[v[1]] = c2;
                    return true;
                }
            }

    return false;
}

//for debug use
template <typename GraphType>
void LPColoring<GraphType>::non_integer_num(vector<GRBVar> const& vColorBits, vector<GRBVar> const& vEdgeBits, LPColoring<GraphType>::NonIntegerInfo& info) const
{
    non_integer_num(vColorBits, info.vertex_non_integer_num, info.vertex_half_integer_num);
    non_integer_num(vEdgeBits, info.edge_non_integer_num, info.edge_half_integer_num);
}

template <typename GraphType>
void LPColoring<GraphType>::non_integer_num(vector<GRBVar> const& vVariables, uint32_t& nonIntegerNum, uint32_t& halfIntegerNum) const 
{
    nonIntegerNum = 0;
    halfIntegerNum = 0;
    for (vector<GRBVar>::const_iterator it = vVariables.begin(), ite = vVariables.end(); it != ite; ++it)
    {
        double value = it->get(GRB_DoubleAttr_X);
        if (value != 0.0 && value != 1.0)
        {
            ++nonIntegerNum;
            if (value == 0.5)
                ++halfIntegerNum;
        }
    }
}

}}} // namespace limbo // namespace algorithms // namespace coloring

#endif
