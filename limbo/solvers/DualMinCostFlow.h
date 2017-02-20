/**
 * @file   DualMinCostFlow.h
 * @author Yibo Lin
 * @date   Feb 2017
 * @brief  Solve a special case of linear programming with dual min-cost flow.
 *         A better implementation of @ref LpDualMcf.h
 */
#ifndef LIMBO_SOLVERS_DUALMINCOSTFLOW_H
#define LIMBO_SOLVERS_DUALMINCOSTFLOW_H

#include <lemon/smart_graph.h>
#include <lemon/network_simplex.h>
#include <lemon/cost_scaling.h>
#include <lemon/capacity_scaling.h>
#include <lemon/cycle_canceling.h>
#include <limbo/solvers/Solvers.h>

/// namespace for Limbo 
namespace limbo 
{
/// namespace for Limbo.Solvers 
namespace solvers 
{

// forward declaration 
class MinCostFlowSolver;

/// @class limbo::solvers::DualMinCostFlow
/// @brief LP solved with min-cost flow. A better implementation of @ref limbo::solvers::lpmcf::LpDualMcf
/// 
/// The dual problem of this LP is a min-cost flow problem, 
/// so we can solve the graph problem and then 
/// call shortest path algrithm to calculate optimum of primal problem. 
///
/// 1. Primal problem \n
/// \f{eqnarray*}{
/// & min. & \sum_{i=1}^{n} c_i \cdot x_i - \sum_{i,j} u_{ij} \alpha_{ij}, \\
/// & s.t. & x_i - x_j - \alpha_{ij} \ge b_{ij}, \forall (i, j) \in E,  \\
/// &     & d_i \le x_i \le u_i, \forall i \in [1, n], \\
/// &     & \alpha_{ij} \ge 0, \forall (i, j) \in A.  
/// \f}
/// \n
/// 2. Introduce new variables \f$y_i\f$ in \f$[0, n]\f$, set \f$x_i = y_i - y_0\f$, \n
/// \f{eqnarray*}{
/// & min. & \sum_{i=1}^{n} c_i \cdot (y_i-y_0) - \sum_{i,j} u_{ij} \alpha_{ij}, \\
/// & s.t. & y_i - y_j -\alpha_{ij} \ge b_{ij}, \forall (i, j) \in E \\
/// &      & d_i \le y_i - y_0 - \alpha_{ij} \le u_i, \forall i \in [1, n], \\
/// &      & y_i \textrm{ is unbounded integer}, \forall i \in [0, n], \\
/// &      & \alpha_{ij} \ge 0, \forall (i, j) \in A.  
/// \f}
/// \n
/// 3. Re-write the problem \n
/// \f{eqnarray*}{
/// & min. & \sum_{i=0}^{n} c_i \cdot y_i - \sum_{i,j} u_{ij} \alpha_{ij}, \textrm{ where } \
///   c_i = \begin{cases}
///             c_i, & \forall i \in [1, n],  \\
///             - \sum_{j=1}^{n} c_i, & i = 0, \\
///           \end{cases} \\
/// & s.t. & y_i - y_j \ge \
///        \begin{cases}
///            b_{ij}, & \forall (i, j) \in E, \\
///            d_i,  & \forall j = 0, i \in [1, n], \\
///            -u_i, & \forall i = 0, j \in [1, n], \\
///        \end{cases} \\
/// &      & y_i \textrm{ is unbounded integer}, \forall i \in [0, n], \\
/// &      & \alpha_{ij} \ge 0, \forall (i, j) \in A.  
/// \f}
/// \n
/// 4. Map to dual min-cost flow problem. \n
///    Let's use \f$c'_i\f$ for generalized \f$c_i\f$ and \f$b'_{ij}\f$ for generalized \f$b_{ij}\f$. \n
///    Then \f$c'_i\f$ is node supply. 
///    For each \f$(i, j) \in E'\f$, an arc from i to j with cost \f$-b'_{ij}\f$ and flow range \f$[0, u_{ij}]\f$. 
///    The variable \f$\alpha_{ij}\f$ denotes the slackness in the primal problem, but the capacity constraint 
///    in the dual problem. We can set the edge capacity as \f$u_{ij}\f$. We can also leave the edge uncapacited (\f$\infty\f$)
///    if there are no such variables. \n
///    Some concolusions from \cite FLOW_B2005_Ahuja where \f$f_{ij}^*\f$ denotes the optimal flow on edge \f$(i, j)\f$ 
///    and \f$c_{ij}^\pi\f$ denotes the reduced cost in the residual network. \n
///    - If \f$c_{ij}^\pi > 0\f$, then \f$f_{ij}^* = 0\f$. 
///    - If \f$ 0 < f_{ij}^* < u_{ij} \f$, then \f$ c_{ij}^\pi = 0 \f$. 
///    - If \f$c_{ij}^\pi < 0\f$, then \f$f_{ij}^* = u_{ij}\f$. 
/// 
///    These conclusions might be useful to check optimality for the primal problem. 
/// \n
/// Caution: this mapping of LP to dual min-cost flow results may result in negative arc cost which is not supported 
/// by all the algorithms (only capacity scaling algorithm supports). Therefore, graph transformation is introduced 
/// to convert arcs with negative costs to positive costs with arc inversal. 
class DualMinCostFlow 
{
    public:
        /// @brief linear model type for the problem 
        typedef LinearModel<int, int> model_type; 
        /// @nowarn
        typedef model_type::coefficient_value_type coefficient_value_type; 
        typedef model_type::variable_value_type variable_value_type; 
        typedef variable_value_type value_type; // use only one kind of value type 
        typedef model_type::variable_type variable_type;
        typedef model_type::constraint_type constraint_type; 
        typedef model_type::expression_type expression_type; 
        typedef model_type::term_type term_type; 
        typedef model_type::property_type property_type;

		typedef lemon::SmartDigraph graph_type;
		typedef graph_type::Node node_type;
		typedef graph_type::Arc arc_type;
		typedef graph_type::NodeMap<value_type> node_value_map_type;
		typedef graph_type::NodeMap<std::string> node_name_map_type;
		typedef graph_type::ArcMap<value_type> arc_value_map_type;
		typedef graph_type::ArcMap<value_type> arc_cost_map_type;
		typedef graph_type::ArcMap<value_type> arc_flow_map_type;
		typedef graph_type::NodeMap<value_type> node_pot_map_type; // potential of each node 
        /// @endnowarn

        /// @brief constructor 
        /// @param model pointer to the model of problem 
        DualMinCostFlow(model_type* model);
        /// @brief destructor 
        ~DualMinCostFlow(); 
        
        /// @brief API to run the algorithm 
        /// @param solver an object to solve min cost flow, use default updater if NULL  
        SolverProperty operator()(MinCostFlowSolver* solver = NULL); 

        /// @return graph 
        graph_type const& graph() const; 
        /// @return arc lower bound map 
        //arc_value_map_type const& lowerMap() const; 
        /// @return arc upper bound map 
        arc_value_map_type const& upperMap() const; 
        /// @return arc cost map 
        arc_cost_map_type const& costMap() const; 
        /// @return node supply map 
        node_value_map_type const& supplyMap() const; 
        /// @return arc flow map 
        arc_flow_map_type& flowMap();
        /// @return node potential map 
        node_pot_map_type& potentialMap(); 
        /// @return total flow cost 
        value_type totalFlowCost() const; 
        /// @param cost total cost of min-cost flow graph 
        void setTotalFlowCost(value_type cost); 
        /// @return total cost of the original LP problem 
        value_type totalCost() const; 

        /// @name print functions to debug.lgf 
        ///@{
        /// @brief print graph 
        /// @param writeSol if true write flow and potential as well 
        void printGraph(bool writeSol) const; 
        ///@}
    protected:
        /// @brief copy constructor, forbidden  
        /// @param rhs right hand side 
        DualMinCostFlow(DualMinCostFlow const& rhs);
        /// @brief assignment, forbidden  
        /// @param rhs right hand side 
        DualMinCostFlow& operator=(DualMinCostFlow const& rhs);

        /// @brief kernel function to solve the problem 
        /// @param solver an object to solve min cost flow, use default updater if NULL  
        SolverProperty solve(MinCostFlowSolver* solver);
        /// @brief prepare data like big M 
        void prepare(); 
        /// @brief build dual min-cost flow graph 
        void buildGraph(); 
        /// @brief map variables and the objective to graph nodes 
        void mapObjective2Graph();
        /// @brief map differential constraints to graph arcs 
        /// @param countArcs flag for counting arcs mode, if true, only count arcs and no actual arcs are added; otherwise, add arcs 
        /// @return number of arcs added 
        unsigned int mapDiffConstraint2Graph(bool countArcs);
        /// @brief map bound constraints to graph arcs 
        /// @param countArcs flag for counting arcs mode, if true, only count arcs and no actual arcs are added; otherwise, add arcs 
        /// @return number of arcs added 
        unsigned int mapBoundConstraint2Graph(bool countArcs);
        /// @brief generalized method to add an arc for differential constraint \f$ x_i - x_j \ge c_{ij} \f$, resolve negative arc costs by arc reversal 
        /// @param xi node corresponding to variable \f$ x_i \f$
        /// @param xj node corresponding to variable \f$ x_j \f$
        /// @param cij constant at right hand side 
        void addArcForDiffConstraint(node_type xi, node_type xj, value_type cij); 
        /// @brief apply solutions to model 
        void applySolution(); 

        model_type* m_model; ///< model for the problem 

		graph_type m_graph; ///< input graph 
		//arc_value_map_type m_mLower; ///< lower bound of flow, usually zero  
		arc_value_map_type m_mUpper; ///< upper bound of flow, arc capacity in min-cost flow  
		arc_cost_map_type m_mCost; ///< arc cost in min-cost flow 
		node_value_map_type m_mSupply; ///< node supply in min-cost flow 
		value_type m_totalFlowCost; ///< total cost after solving 
        value_type m_bigM; ///< a big number for infinity 
        value_type m_reversedArcFlowCost; ///< normalized flow cost of overall reversed arcs to resolve negative arc cost, to get total flow cost of reversed arcs, it needs to times with big M 

		arc_flow_map_type m_mFlow; ///< solution of min-cost flow, which is the dual solution of LP 
		node_pot_map_type m_mPotential; ///< dual solution of min-cost flow, which is the solution of LP 
};

/// @brief A base class of min-cost flow solver 
class MinCostFlowSolver
{
    public:
        /// @brief constructor 
        MinCostFlowSolver(); 
        /// @brief copy constructor 
        /// @param rhs right hand side 
        MinCostFlowSolver(MinCostFlowSolver const& rhs); 
        /// @brief assignment 
        /// @param rhs right hand side 
        MinCostFlowSolver& operator=(MinCostFlowSolver const& rhs); 
        /// @brief destructor 
        virtual ~MinCostFlowSolver();

        /// @brief API to run min-cost flow solver 
        /// @param d dual min-cost flow object 
        virtual SolverProperty operator()(DualMinCostFlow* d) = 0; 
    protected:
        /// @brief copy object 
        void copy(MinCostFlowSolver const& rhs); 
};

/// @brief Capacity scaling algorithm for min-cost flow 
class CapacityScaling : public MinCostFlowSolver
{
    public:
        /// @brief base type 
        typedef MinCostFlowSolver base_type; 
        /// @brief algorithm type 
        typedef lemon::CapacityScaling<DualMinCostFlow::graph_type, 
                DualMinCostFlow::value_type, 
                DualMinCostFlow::value_type> alg_type;

        /// @brief constructor 
        /// @param factor scaling factor 
        CapacityScaling(int factor = 4);
        /// @brief copy constructor 
        /// @param rhs right hand side 
        CapacityScaling(CapacityScaling const& rhs); 
        /// @brief assignment 
        /// @param rhs right hand side 
        CapacityScaling& operator=(CapacityScaling const& rhs); 

        /// @brief API to run min-cost flow solver 
        /// @param d dual min-cost flow object 
        virtual SolverProperty operator()(DualMinCostFlow* d); 
    protected:
        /// @brief copy object 
        void copy(CapacityScaling const& rhs); 

        int m_factor; ///< scaling factor for the algorithm 
};

/// @brief Cost scaling algorithm for min-cost flow 
class CostScaling : public MinCostFlowSolver
{
    public:
        /// @brief base type 
        typedef MinCostFlowSolver base_type; 
        /// @brief algorithm type 
        typedef lemon::CostScaling<DualMinCostFlow::graph_type, 
                DualMinCostFlow::value_type, 
                DualMinCostFlow::value_type> alg_type;

        /// @brief constructor 
        /// @param method internal method 
        /// @param factor scaling factor 
        CostScaling(alg_type::Method method = alg_type::PARTIAL_AUGMENT, int factor = 16);
        /// @brief copy constructor 
        /// @param rhs right hand side 
        CostScaling(CostScaling const& rhs); 
        /// @brief assignment 
        /// @param rhs right hand side 
        CostScaling& operator=(CostScaling const& rhs); 

        /// @brief API to run min-cost flow solver 
        /// @param d dual min-cost flow object 
        virtual SolverProperty operator()(DualMinCostFlow* d); 
    protected:
        /// @brief copy object 
        void copy(CostScaling const& rhs); 

        alg_type::Method m_method; ///< PUSH, AUGMENT, PARTIAL_AUGMENT
        int m_factor; ///< scaling factor for the algorithm 
};

/// @brief Network simplex algorithm for min-cost flow 
class NetworkSimplex : public MinCostFlowSolver
{
    public:
        /// @brief base type 
        typedef MinCostFlowSolver base_type; 
        /// @brief algorithm type 
        typedef lemon::NetworkSimplex<DualMinCostFlow::graph_type, 
                DualMinCostFlow::value_type, 
                DualMinCostFlow::value_type> alg_type;

        /// @brief constructor 
        /// @param pivotRule pivot rule 
        NetworkSimplex(alg_type::PivotRule pivotRule = alg_type::BLOCK_SEARCH);
        /// @brief copy constructor 
        /// @param rhs right hand side 
        NetworkSimplex(NetworkSimplex const& rhs); 
        /// @brief assignment 
        /// @param rhs right hand side 
        NetworkSimplex& operator=(NetworkSimplex const& rhs); 

        /// @brief API to run min-cost flow solver 
        /// @param d dual min-cost flow object 
        virtual SolverProperty operator()(DualMinCostFlow* d); 
    protected:
        /// @brief copy object 
        void copy(NetworkSimplex const& rhs); 

        alg_type::PivotRule m_pivotRule; ///< pivot rule for the algorithm, FIRST_ELIGIBLE, BEST_ELIGIBLE, BLOCK_SEARCH, CANDIDATE_LIST, ALTERING_LIST
};

/// @brief Cycle canceling algorithm for min-cost flow 
class CycleCanceling : public MinCostFlowSolver
{
    public:
        /// @brief base type 
        typedef MinCostFlowSolver base_type; 
        /// @brief algorithm type 
        typedef lemon::CycleCanceling<DualMinCostFlow::graph_type, 
                DualMinCostFlow::value_type, 
                DualMinCostFlow::value_type> alg_type;

        /// @brief constructor 
        /// @param method method
        CycleCanceling(alg_type::Method method = alg_type::CANCEL_AND_TIGHTEN);
        /// @brief copy constructor 
        /// @param rhs right hand side 
        CycleCanceling(CycleCanceling const& rhs); 
        /// @brief assignment 
        /// @param rhs right hand side 
        CycleCanceling& operator=(CycleCanceling const& rhs); 

        /// @brief API to run min-cost flow solver 
        /// @param d dual min-cost flow object 
        virtual SolverProperty operator()(DualMinCostFlow* d); 
    protected:
        /// @brief copy object 
        void copy(CycleCanceling const& rhs); 

        alg_type::Method m_method; ///< method for the algorithm, SIMPLE_CYCLE_CANCELING, MINIMUM_MEAN_CYCLE_CANCELING, CANCEL_AND_TIGHTEN
};


} // namespace solvers 
} // namespace limbo 

#endif
