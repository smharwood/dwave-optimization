// Copyright 2024 D-Wave
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "dwave-optimization/graph.hpp"
#include "dwave-optimization/nodes/collections.hpp"
#include "dwave-optimization/nodes/constants.hpp"
#include "dwave-optimization/nodes/indexing.hpp"
#include "dwave-optimization/nodes/mathematical.hpp"
#include "dwave-optimization/nodes/numbers.hpp"
#include "dwave-optimization/nodes/testing.hpp"
#include "../../utils.hpp"

namespace dwave::optimization {

TEST_CASE("AdvancedIndexingNode") {
    auto graph = Graph();

    GIVEN("A 2d NxN matrix with two const 1d index arrays") {
        std::vector<double> values = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        auto arr_ptr =
                graph.emplace_node<ConstantNode>(values, std::initializer_list<ssize_t>{3, 3});

        std::vector<double> i = {0, 1, 2};
        auto i_ptr = graph.emplace_node<ConstantNode>(i);

        std::vector<double> j = {1, 2, 0};
        auto j_ptr = graph.emplace_node<ConstantNode>(j);

        WHEN("We access the matrix by the indices") {
            auto out_ptr = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, j_ptr);

            THEN("We get the shape we expect") {
                CHECK(out_ptr->size() == 3);
                CHECK(std::ranges::equal(out_ptr->shape(), std::vector{3}));

                CHECK(array_shape_equal(i_ptr, out_ptr));
                CHECK(array_shape_equal(j_ptr, out_ptr));
            }

            THEN("We see the predecessors we expect") {
                CHECK(std::ranges::equal(out_ptr->predecessors(),
                                         std::vector<Node*>{arr_ptr, i_ptr, j_ptr}));
            }

            THEN("We see the min/max/integral we expect") {
                CHECK(out_ptr->min() == 0);
                CHECK(out_ptr->max() == 8);
                CHECK(out_ptr->integral());
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 1, 2}));
                    CHECK(std::ranges::equal(j_ptr->view(state), std::vector{1, 2, 0}));
                    CHECK(std::ranges::equal(out_ptr->view(state), std::vector{1, 5, 6}));
                }
            }
        }
    }

    GIVEN("A 1d length 5 array accessed by a SetNode(5)") {
        std::vector<double> values{4, 3, 2, 1, 0};
        auto A_ptr = graph.emplace_node<ConstantNode>(values);
        auto s_ptr = graph.emplace_node<SetNode>(5);
        auto B_ptr = graph.emplace_node<AdvancedIndexingNode>(A_ptr, s_ptr);

        THEN("The resulting array has the size/shape we expect") {
            CHECK(B_ptr->dynamic());
            CHECK(std::ranges::equal(B_ptr->shape(), std::vector{-1}));
            CHECK(B_ptr->size() == Array::DYNAMIC_SIZE);
            CHECK(std::ranges::equal(B_ptr->strides(), std::vector{sizeof(double)}));
            CHECK(B_ptr->ndim() == 1);

            CHECK(array_shape_equal(B_ptr, s_ptr));
        }

        THEN("We see the min/max/integral we expect") {
            CHECK(B_ptr->min() == 0);
            CHECK(B_ptr->max() == 4);
            CHECK(B_ptr->integral());
        }

        WHEN("We default-initialize the state") {
            auto state = graph.initialize_state();

            THEN("We have the state we expect") {
                CHECK(std::ranges::equal(A_ptr->view(state), values));
                CHECK(std::ranges::equal(s_ptr->view(state), std::vector<double>{}));
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector<double>{}));
            }

            THEN("The resulting array has the same size as the SetNode") {
                CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                CHECK(B_ptr->size(state) == s_ptr->size(state));
            }

            AND_WHEN("We grow the SetNode once") {
                s_ptr->grow(state);
                s_ptr->propagate(state);
                B_ptr->propagate(state);  // so any changes are incorporated

                THEN("The state is updated and the updates are signalled") {
                    CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                    CHECK(B_ptr->size(state) == s_ptr->size(state));

                    CHECK(std::ranges::equal(B_ptr->view(state), std::vector{4}));

                    verify_array_diff({}, {4}, B_ptr->diff(state));
                }

                AND_WHEN("We commit") {
                    s_ptr->commit(state);
                    B_ptr->commit(state);

                    THEN("The values stick, and the diff is cleared") {
                        CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                        CHECK(B_ptr->size(state) == s_ptr->size(state));
                        CHECK(std::ranges::equal(B_ptr->view(state), std::vector{4}));

                        CHECK(B_ptr->size_diff(state) == 0);
                        CHECK(B_ptr->diff(state).size() == 0);
                    }
                }

                AND_WHEN("We revert") {
                    s_ptr->revert(state);
                    B_ptr->revert(state);

                    THEN("We're back to where we started") {
                        CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                        CHECK(B_ptr->size(state) == s_ptr->size(state));
                        CHECK(std::ranges::equal(B_ptr->view(state), std::vector<double>{}));

                        CHECK(B_ptr->size_diff(state) == 0);
                        CHECK(B_ptr->diff(state).size() == 0);
                    }
                }
            }

            AND_WHEN("We grow the SetNode twice") {
                s_ptr->grow(state);
                s_ptr->grow(state);
                s_ptr->propagate(state);
                B_ptr->propagate(state);  // so any changes are incorporated

                THEN("The state is updated and the updates are signalled") {
                    CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                    CHECK(B_ptr->size(state) == s_ptr->size(state));

                    CHECK(std::ranges::equal(B_ptr->view(state), std::vector{4, 3}));

                    CHECK(B_ptr->size_diff(state) == 2);  // grew by two

                    verify_array_diff({}, {4, 3}, B_ptr->diff(state));
                }

                AND_WHEN("We commit") {
                    s_ptr->commit(state);
                    B_ptr->commit(state);

                    THEN("The values stick, and the diff is cleared") {
                        CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                        CHECK(B_ptr->size(state) == s_ptr->size(state));
                        CHECK(std::ranges::equal(B_ptr->view(state), std::vector{4, 3}));

                        CHECK(B_ptr->size_diff(state) == 0);
                        CHECK(B_ptr->diff(state).size() == 0);
                    }

                    AND_WHEN("We shrink the SetNode once") {
                        s_ptr->shrink(state);
                        s_ptr->propagate(state);
                        B_ptr->propagate(state);  // so any changes are incorporated

                        THEN("The state is updated and the updates are signalled") {
                            CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                            CHECK(B_ptr->size(state) == s_ptr->size(state));

                            CHECK(std::ranges::equal(B_ptr->view(state), std::vector{4}));

                            CHECK(B_ptr->size_diff(state) == -1);  // shrank by one

                            verify_array_diff({4, 3}, {4}, B_ptr->diff(state));
                        }

                        AND_WHEN("We commit") {
                            s_ptr->commit(state);
                            B_ptr->commit(state);

                            THEN("The values stick, and the diff is cleared") {
                                CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                                CHECK(B_ptr->size(state) == s_ptr->size(state));
                                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{4}));

                                CHECK(B_ptr->size_diff(state) == 0);
                                CHECK(B_ptr->diff(state).size() == 0);
                            }
                        }

                        AND_WHEN("We revert") {
                            s_ptr->revert(state);
                            B_ptr->revert(state);

                            THEN("We're back to where we started") {
                                CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                                CHECK(B_ptr->size(state) == s_ptr->size(state));
                                CHECK(std::ranges::equal(B_ptr->view(state),
                                                         std::vector<double>{4, 3}));

                                CHECK(B_ptr->size_diff(state) == 0);
                                CHECK(B_ptr->diff(state).size() == 0);
                            }
                        }
                    }
                }

                AND_WHEN("We revert") {
                    s_ptr->revert(state);
                    B_ptr->revert(state);

                    THEN("We're back to where we started") {
                        CHECK(std::ranges::equal(B_ptr->shape(state), s_ptr->shape(state)));
                        CHECK(B_ptr->size(state) == s_ptr->size(state));
                        CHECK(std::ranges::equal(B_ptr->view(state), std::vector<double>{}));

                        CHECK(B_ptr->size_diff(state) == 0);
                        CHECK(B_ptr->diff(state).size() == 0);
                    }
                }
            }
        }
    }

    GIVEN("A 2d 3x3 matrix accessed by two List(3) nodes") {
        std::vector<double> values{0, 1, 2, 3, 4, 5, 6, 7, 8};
        auto A_ptr = graph.emplace_node<ConstantNode>(values, std::initializer_list<ssize_t>{3, 3});

        auto i_ptr = graph.emplace_node<ListNode>(3);
        auto j_ptr = graph.emplace_node<ListNode>(3);

        auto B_ptr = graph.emplace_node<AdvancedIndexingNode>(A_ptr, i_ptr, j_ptr);

        THEN("Then the resulting matrix has the size/shape we expect") {
            CHECK(std::ranges::equal(B_ptr->shape(), std::vector{3}));
            CHECK(B_ptr->size() == 3);
            CHECK(B_ptr->ndim() == 1);

            CHECK(array_shape_equal(B_ptr, i_ptr));
            CHECK(array_shape_equal(B_ptr, j_ptr));
        }

        THEN("We see the min/max/integral we expect") {
            CHECK(B_ptr->min() == 0);
            CHECK(B_ptr->max() == 8);
            CHECK(B_ptr->integral());
        }

        WHEN("We default-initialize the state") {
            auto state = graph.initialize_state();

            THEN("We have the state we expect") {
                CHECK(std::ranges::equal(A_ptr->view(state), values));
                CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 1, 2}));
                CHECK(std::ranges::equal(j_ptr->view(state), std::vector{0, 1, 2}));
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{0, 4, 8}));
            }
        }

        WHEN("We explicitly initialize the state") {
            auto state = graph.empty_state();
            i_ptr->initialize_state(state, {0, 2, 1});
            j_ptr->initialize_state(state, {2, 1, 0});
            graph.initialize_state(state);

            THEN("We have the state we expect") {
                CHECK(std::ranges::equal(A_ptr->view(state), values));
                CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 2, 1}));
                CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1, 0}));
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7, 3}));
            }

            AND_WHEN("We mutate one of the decision variables and then propagate") {
                i_ptr->exchange(state, 1, 2);  // [0, 2, 1] -> [0, 1, 2]

                i_ptr->propagate(state);
                B_ptr->propagate(state);

                THEN("We have the state we expect") {
                    CHECK(std::ranges::equal(A_ptr->view(state), values));
                    CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 1, 2}));
                    CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1, 0}));
                    CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 4, 6}));

                    verify_array_diff({2, 7, 3}, {2, 4, 6}, B_ptr->diff(state));
                }

                AND_WHEN("We commit") {
                    i_ptr->commit(state);
                    B_ptr->commit(state);

                    THEN("We have the updated state still") {
                        CHECK(std::ranges::equal(A_ptr->view(state), values));
                        CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 1, 2}));
                        CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1, 0}));
                        CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 4, 6}));
                    }
                }

                AND_WHEN("We revert") {
                    i_ptr->revert(state);
                    B_ptr->revert(state);

                    THEN("We have the original state") {
                        CHECK(std::ranges::equal(A_ptr->view(state), values));
                        CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 2, 1}));
                        CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1, 0}));
                        CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7, 3}));
                    }
                }
            }
        }
    }

    GIVEN("A 2d 3x3 matrix accessed by two dynamic nodes") {
        std::vector<double> values{0, 1, 2, 3, 4, 5, 6, 7, 8};
        auto A_ptr = graph.emplace_node<ConstantNode>(values, std::initializer_list<ssize_t>{3, 3});

        auto dyn_ptr = graph.emplace_node<DynamicArrayTestingNode>(
                std::initializer_list<ssize_t>{-1, 2}, 0, 2, true);

        auto i_ptr = graph.emplace_node<BasicIndexingNode>(dyn_ptr, Slice(), 0);
        auto j_ptr = graph.emplace_node<BasicIndexingNode>(dyn_ptr, Slice(), 1);

        auto B_ptr = graph.emplace_node<AdvancedIndexingNode>(A_ptr, i_ptr, j_ptr);

        THEN("Then the resulting matrix has the size/shape we expect") {
            CHECK(B_ptr->dynamic());
            CHECK(B_ptr->ndim() == 1);

            CHECK(array_shape_equal(B_ptr, i_ptr));
            CHECK(array_shape_equal(B_ptr, j_ptr));
        }

        WHEN("We explicitly initialize an empty state") {
            auto state = graph.empty_state();
            dyn_ptr->initialize_state(state);
            graph.initialize_state(state);

            THEN("We have the state we expect") {
                CHECK(B_ptr->dynamic());
                CHECK(B_ptr->size(state) == 0);
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector<double>()));
            }

            AND_WHEN("We grow both of the decision variables and then propagate") {
                // i_ptr->grow(state);  // [] -> [0]
                // j_ptr->grow(state);  // [] -> [0]
                dyn_ptr->grow(state, {0, 0});

                dyn_ptr->propagate(state);
                i_ptr->propagate(state);
                j_ptr->propagate(state);
                B_ptr->propagate(state);

                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{0}));
                verify_array_diff({}, {0}, B_ptr->diff(state));
            }
        }

        WHEN("We explicitly initialize a state") {
            auto state = graph.empty_state();
            // i_ptr = [0, 2]
            // j_ptr = [2, 1]
            dyn_ptr->initialize_state(state, {0, 2, 2, 1});
            graph.initialize_state(state);

            THEN("We have the state we expect") {
                CHECK(dyn_ptr->size(state) == 4);
                CHECK(std::ranges::equal(dyn_ptr->shape(state), std::vector{2, 2}));

                CHECK(i_ptr->size(state) == 2);
                CHECK(std::ranges::equal(i_ptr->shape(state), std::vector{2}));
                CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 2}));

                CHECK(j_ptr->size(state) == 2);
                CHECK(std::ranges::equal(j_ptr->shape(state), std::vector{2}));
                CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1}));

                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7}));
            }

            AND_WHEN("We grow the decision variable and then propagate") {
                // i_ptr [0, 2] -> [0, 2, 1]
                // j_ptr [2, 1] -> [2, 1, 0]
                dyn_ptr->grow(state, {1, 0});

                dyn_ptr->propagate(state);
                i_ptr->propagate(state);
                j_ptr->propagate(state);
                B_ptr->propagate(state);

                CHECK(std::ranges::equal(dyn_ptr->view(state), std::vector{0, 2, 2, 1, 1, 0}));
                CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 2, 1}));
                CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1, 0}));
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7, 3}));
                verify_array_diff({2, 7}, {2, 7, 3}, B_ptr->diff(state));

                AND_WHEN("We revert") {
                    dyn_ptr->revert(state);
                    i_ptr->revert(state);
                    j_ptr->revert(state);
                    B_ptr->revert(state);

                    CHECK(std::ranges::equal(dyn_ptr->view(state), std::vector{0, 2, 2, 1}));
                    CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 2}));
                    CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1}));
                    CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7}));
                    CHECK(B_ptr->diff(state).size() == 0);
                }
            }

            AND_WHEN("We shrink both of the decision variables and then propagate") {
                // i_ptr [0, 2] -> [0]
                // j_ptr [2, 1] -> [2]
                dyn_ptr->shrink(state);

                dyn_ptr->propagate(state);
                i_ptr->propagate(state);
                j_ptr->propagate(state);
                B_ptr->propagate(state);

                CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0}));
                CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2}));
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2}));
                verify_array_diff({2, 7}, {2}, B_ptr->diff(state));

                AND_WHEN("We revert") {
                    dyn_ptr->revert(state);
                    i_ptr->revert(state);
                    j_ptr->revert(state);
                    B_ptr->revert(state);

                    CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7}));
                    CHECK(B_ptr->diff(state).size() == 0);
                }
            }

            AND_WHEN("We change and shrink") {
                // i_ptr [0, 2] -> [0]
                // j_ptr [2, 1] -> [1]
                dyn_ptr->set(state, 1, 1);
                dyn_ptr->shrink(state);

                dyn_ptr->propagate(state);
                i_ptr->propagate(state);
                j_ptr->propagate(state);
                B_ptr->propagate(state);

                CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0}));
                CHECK(std::ranges::equal(j_ptr->view(state), std::vector{1}));
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{1}));
                verify_array_diff({2, 7}, {1}, B_ptr->diff(state));

                AND_WHEN("We revert") {
                    dyn_ptr->revert(state);
                    i_ptr->revert(state);
                    j_ptr->revert(state);
                    B_ptr->revert(state);

                    CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7}));
                    CHECK(B_ptr->diff(state).size() == 0);
                }
            }

            AND_WHEN("We grow, update, shrink, then propagate") {
                // i_ptr [0, 2] -> [0, 2, 1] -> [0, 2]
                // j_ptr [2, 1] -> [2, 1, 0] -> [0, 1, 2] -> [2, 1, 0] -> [2, 1]
                dyn_ptr->grow(state, {1, 0});
                dyn_ptr->set(state, 1, 0);
                dyn_ptr->set(state, 5, 2);
                dyn_ptr->set(state, 1, 2);
                dyn_ptr->set(state, 5, 0);
                dyn_ptr->shrink(state);

                dyn_ptr->propagate(state);
                i_ptr->propagate(state);
                j_ptr->propagate(state);
                B_ptr->propagate(state);

                CHECK(std::ranges::equal(i_ptr->view(state), std::vector{0, 2}));
                CHECK(std::ranges::equal(j_ptr->view(state), std::vector{2, 1}));
                CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7}));
                verify_array_diff({2, 7}, {2, 7}, B_ptr->diff(state));

                AND_WHEN("We revert") {
                    dyn_ptr->revert(state);
                    i_ptr->revert(state);
                    j_ptr->revert(state);
                    B_ptr->revert(state);

                    CHECK(std::ranges::equal(B_ptr->view(state), std::vector{2, 7}));
                    CHECK(B_ptr->diff(state).size() == 0);
                }
            }
        }
    }

    GIVEN("A 3d 2x3x5 matrix with two const 1d index arrays") {
        std::vector<double> values(30);
        std::iota(values.begin(), values.end(), 0);
        auto arr_ptr =
                graph.emplace_node<ConstantNode>(values, std::initializer_list<ssize_t>{2, 3, 5});

        std::vector<double> i = {1, 0};
        auto i_ptr = graph.emplace_node<ConstantNode>(i);

        std::vector<double> j = {1, 1};
        auto j_ptr = graph.emplace_node<ConstantNode>(j);

        WHEN("We access the matrix by (i, j, :)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, j_ptr, Slice());

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 10);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 5}));
            }

            THEN("We see the predecessors we expect") {
                CHECK(std::ranges::equal(adv->predecessors(),
                                         std::vector<Node*>{arr_ptr, i_ptr, j_ptr}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(adv->view(state),
                                             std::vector{20, 21, 22, 23, 24, 5, 6, 7, 8, 9}));
                }
            }
        }

        WHEN("We access the matrix by (i, :, j)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, Slice(), j_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 6);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 3}));
            }

            THEN("We see the predecessors we expect") {
                CHECK(std::ranges::equal(adv->predecessors(),
                                         std::vector<Node*>{arr_ptr, i_ptr, j_ptr}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(adv->view(state), std::vector{16, 21, 26, 1, 6, 11}));
                }
            }
        }
    }

    GIVEN("A 4d 2x3x5x4 matrix with three const 1d index arrays") {
        std::vector<double> values(2 * 3 * 5 * 4);
        std::iota(values.begin(), values.end(), 0);
        auto arr_ptr = graph.emplace_node<ConstantNode>(values,
                                                        std::initializer_list<ssize_t>{2, 3, 5, 4});

        std::vector<double> i = {1, 0};
        auto i_ptr = graph.emplace_node<ConstantNode>(i);

        std::vector<double> j = {1, 1};
        auto j_ptr = graph.emplace_node<ConstantNode>(j);

        std::vector<double> k = {1, 2};
        auto k_ptr = graph.emplace_node<ConstantNode>(k);

        WHEN("We access the matrix by (i, j, k, :)") {
            auto adv =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, j_ptr, k_ptr, Slice());

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 8);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 4}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(adv->view(state),
                                             std::vector{84, 85, 86, 87, 28, 29, 30, 31}));
                }
            }
        }

        WHEN("We access the matrix by (i, j, :, k)") {
            auto adv =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, j_ptr, Slice(), k_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 10);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 5}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(adv->view(state),
                                             std::vector{81, 85, 89, 93, 97, 22, 26, 30, 34, 38}));
                }
            }
        }

        WHEN("We access the matrix by (i, :, j, k)") {
            auto adv =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, Slice(), j_ptr, k_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 6);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 3}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(adv->view(state),
                                             std::vector{65, 85, 105, 6, 26, 46}));
                }
            }
        }

        WHEN("We access the matrix by (i, :, :, k)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, Slice(), Slice(),
                                                                k_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 30);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 3, 5}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(
                            adv->view(state),
                            std::vector{61,  65,  69,  73,  77,  81, 85, 89, 93, 97,
                                        101, 105, 109, 113, 117, 2,  6,  10, 14, 18,
                                        22,  26,  30,  34,  38,  42, 46, 50, 54, 58}));
                }
            }
        }

        WHEN("We access the matrix by (i, :, k, :)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, Slice(), k_ptr,
                                                                Slice());

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 24);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 3, 4}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(
                            adv->view(state),
                            std::vector{64, 65, 66, 67, 84, 85, 86, 87, 104, 105, 106, 107,
                                        8,  9,  10, 11, 28, 29, 30, 31, 48,  49,  50,  51}));
                }
            }
        }

        WHEN("We access the matrix by (:, i, k, :)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, k_ptr,
                                                                Slice());

            THEN("We get the shape we expect") {
                CHECK(adv->size() == 16);
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 2, 4}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("We can read out the state of the nodes") {
                    CHECK(std::ranges::equal(arr_ptr->view(state), values));
                    CHECK(std::ranges::equal(adv->view(state),
                                             std::vector{24, 25, 26, 27, 8, 9, 10, 11, 84, 85, 86,
                                                         87, 68, 69, 70, 71}));
                }
            }
        }
    }

    GIVEN("A 4d 2x3x5x4 matrix with 3 dynamic indexing arrays") {
        std::vector<double> values(2 * 3 * 5 * 4);
        std::iota(values.begin(), values.end(), 0);
        auto arr_ptr = graph.emplace_node<ConstantNode>(values,
                                                        std::initializer_list<ssize_t>{2, 3, 5, 4});
        auto dyn_ptr = graph.emplace_node<DynamicArrayTestingNode>(
                std::initializer_list<ssize_t>{-1, 3}, 0, 1, true);

        auto i_ptr = graph.emplace_node<BasicIndexingNode>(dyn_ptr, Slice(), 0);
        auto j_ptr = graph.emplace_node<BasicIndexingNode>(dyn_ptr, Slice(), 1);
        auto k_ptr = graph.emplace_node<BasicIndexingNode>(dyn_ptr, Slice(), 2);

        WHEN("We access the matrix by (i, j, k, :)") {
            auto adv =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, j_ptr, k_ptr, Slice());

            THEN("We get the shape we expect") {
                CHECK(adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{-1, 4}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("The state starts empty") {
                    CHECK(std::ranges::equal(adv->view(state), std::vector<double>{}));
                }

                AND_WHEN("We grow the indexing nodes and propagate") {
                    // i_ptr -> {0, 1}
                    // j_ptr -> {1, 2}
                    // k_ptr -> {4, 4}
                    dyn_ptr->grow(state, {0, 1, 4, 1, 2, 4});

                    dyn_ptr->propagate(state);
                    i_ptr->propagate(state);
                    j_ptr->propagate(state);
                    k_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 8);
                        CHECK(std::ranges::equal(adv->view(state),
                                                 std::vector{36, 37, 38, 39, 116, 117, 118, 119}));
                        verify_array_diff({}, {36, 37, 38, 39, 116, 117, 118, 119},
                                          adv->diff(state));
                    }

                    AND_WHEN("We shrink the indexing nodes and propagate") {
                        dyn_ptr->commit(state);
                        i_ptr->commit(state);
                        j_ptr->commit(state);
                        k_ptr->commit(state);
                        adv->commit(state);

                        dyn_ptr->shrink(state);

                        dyn_ptr->propagate(state);
                        i_ptr->propagate(state);
                        j_ptr->propagate(state);
                        k_ptr->propagate(state);
                        adv->propagate(state);

                        THEN("The state has the expected values and the diff is correct") {
                            CHECK(adv->size(state) == 4);
                            CHECK(std::ranges::equal(adv->view(state),
                                                     std::vector{36, 37, 38, 39}));
                            verify_array_diff({36, 37, 38, 39, 116, 117, 118, 119},
                                              {36, 37, 38, 39}, adv->diff(state));
                        }

                        AND_WHEN("We revert") {
                            dyn_ptr->revert(state);
                            i_ptr->revert(state);
                            j_ptr->revert(state);
                            k_ptr->revert(state);
                            adv->revert(state);

                            THEN("The state has returned to the original") {
                                CHECK(adv->size(state) == 8);
                                CHECK(std::ranges::equal(
                                        adv->view(state),
                                        std::vector{36, 37, 38, 39, 116, 117, 118, 119}));
                                CHECK(adv->diff(state).size() == 0);
                            }
                        }
                    }
                }
            }
        }

        WHEN("We access the matrix by (i, :, j, k)") {
            auto adv =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, Slice(), j_ptr, k_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{-1, 3}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.initialize_state();

                THEN("The state starts empty") {
                    CHECK(std::ranges::equal(adv->view(state), std::vector<double>{}));
                }

                AND_WHEN("We grow the indexing nodes and propagate") {
                    // i_ptr -> {0, 1}
                    // j_ptr -> {1, 2}
                    // k_ptr -> {3, 3}
                    dyn_ptr->grow(state, {0, 1, 3, 1, 2, 3});

                    dyn_ptr->propagate(state);
                    i_ptr->propagate(state);
                    j_ptr->propagate(state);
                    k_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 6);
                        CHECK(std::ranges::equal(adv->view(state),
                                                 std::vector{7, 27, 47, 71, 91, 111}));
                        verify_array_diff({}, {7, 27, 47, 71, 91, 111}, adv->diff(state));
                    }

                    AND_WHEN("We shrink the indexing nodes and propagate") {
                        dyn_ptr->commit(state);
                        i_ptr->commit(state);
                        j_ptr->commit(state);
                        k_ptr->commit(state);
                        adv->commit(state);

                        dyn_ptr->shrink(state);

                        dyn_ptr->propagate(state);
                        i_ptr->propagate(state);
                        j_ptr->propagate(state);
                        k_ptr->propagate(state);
                        adv->propagate(state);

                        THEN("The state has the expected values and the diff is correct") {
                            CHECK(adv->size(state) == 3);
                            CHECK(std::ranges::equal(adv->view(state), std::vector{7, 27, 47}));
                            verify_array_diff({7, 27, 47, 71, 91, 111}, {7, 27, 47},
                                              adv->diff(state));
                        }

                        AND_WHEN("We revert") {
                            dyn_ptr->revert(state);
                            i_ptr->revert(state);
                            j_ptr->revert(state);
                            k_ptr->revert(state);
                            adv->revert(state);

                            THEN("The state has returned to the original") {
                                CHECK(adv->size(state) == 6);
                                CHECK(std::ranges::equal(adv->view(state),
                                                         std::vector{7, 27, 47, 71, 91, 111}));
                                CHECK(adv->diff(state).size() == 0);
                            }
                        }
                    }
                }
            }
        }
    }

    GIVEN("A dynamic 4d Nx3x5x4 matrix with 3 dynamic indexing arrays") {
        auto arr_ptr = graph.emplace_node<DynamicArrayTestingNode>(
                std::initializer_list<ssize_t>{-1, 3, 5, 4},  //
                -180, 180, true,  // takes values in [-180, 180] and will always be integers
                120, 1200);  // will always have at least two rows in the first dimension, up to 20

        auto i_range_ptr =
                graph.emplace_node<ConstantNode>(std::vector{0, 1});  // will index axis 1
        auto j_range_ptr = graph.emplace_node<ConstantNode>(std::vector{0, 1, 2, 3, 4});  // axis 3
        auto k_range_ptr = graph.emplace_node<ConstantNode>(std::vector{0, 1, 2, 3});     // axis 4

        auto dyn_ptr = graph.emplace_node<ListNode>(2, 0, 2);  // subsets of range(2)

        auto i_ptr = graph.emplace_node<AdvancedIndexingNode>(i_range_ptr, dyn_ptr);
        auto j_ptr = graph.emplace_node<AdvancedIndexingNode>(j_range_ptr, dyn_ptr);
        auto k_ptr = graph.emplace_node<AdvancedIndexingNode>(k_range_ptr, dyn_ptr);

        // toss a few validation nodes on for safey
        graph.emplace_node<ArrayValidationNode>(i_ptr);
        graph.emplace_node<ArrayValidationNode>(j_ptr);
        graph.emplace_node<ArrayValidationNode>(k_ptr);

        WHEN("We access the matrix by (i, :, j, k)") {
            auto adv_ptr =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, Slice(), j_ptr, k_ptr);

            graph.emplace_node<ArrayValidationNode>(adv_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv_ptr->dynamic());
                CHECK(std::ranges::equal(adv_ptr->shape(), std::vector{-1, 3}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.empty_state();

                // as promised, but 120 values into the array initially
                std::vector<double> values(2 * 3 * 5 * 4);
                std::iota(values.begin(), values.end(), 0);
                arr_ptr->initialize_state(state, values);

                // dyn array starts empty
                dyn_ptr->initialize_state(state, std::vector<double>{});

                graph.initialize_state(state);

                AND_WHEN("We mutate the main array, grow the indexing nodes and propagate") {
                    // double the size of the array
                    arr_ptr->grow(state, values);
                    dyn_ptr->grow(state);

                    // Change should be visible
                    arr_ptr->set(state, 20, -1);

                    // Not present in the ranges indexed
                    arr_ptr->set(state, 41, -2);

                    graph.propagate(state, graph.descendants(state, {arr_ptr, dyn_ptr}));

                    AND_WHEN("We commit") {
                        graph.commit(state, graph.descendants(state, {arr_ptr, dyn_ptr}));

                        THEN("The output is as expected") {
                            CHECK(std::ranges::equal(adv_ptr->view(state), std::vector{0, -1, 40}));
                            // ArrayValidationNode checks most of the consistency etc
                        }
                    }

                    AND_WHEN("We revert") {
                        graph.revert(state, graph.descendants(state, {arr_ptr, dyn_ptr}));

                        THEN("The output is as expected") {
                            CHECK(std::ranges::equal(adv_ptr->view(state), std::vector<double>{}));
                            // ArrayValidationNode checks most of the consistency etc
                        }
                    }
                }
            }
        }

        WHEN("We access the matrix by (i, :, j, ;)") {
            auto adv_ptr = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, Slice(), j_ptr,
                                                                    Slice());

            graph.emplace_node<ArrayValidationNode>(adv_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv_ptr->dynamic());
                CHECK(std::ranges::equal(adv_ptr->shape(), std::vector{-1, 3, 4}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.empty_state();

                // as promised, but 120 values into the array initially
                std::vector<double> values(2 * 3 * 5 * 4);
                std::iota(values.begin(), values.end(), 0);
                arr_ptr->initialize_state(state, values);

                // dyn array starts empty
                dyn_ptr->initialize_state(state, std::vector<double>{});

                graph.initialize_state(state);

                AND_WHEN("We mutate the main array, grow the indexing nodes and propagate") {
                    // double the size of the array
                    arr_ptr->grow(state, values);
                    dyn_ptr->grow(state);

                    // These changes should be visible
                    arr_ptr->set(state, 21, -1);
                    arr_ptr->set(state, 42, -2);

                    // Not present in the ranges indexed
                    arr_ptr->set(state, 8, -4);

                    graph.propagate(state, graph.descendants(state, {arr_ptr, dyn_ptr}));

                    AND_WHEN("We commit") {
                        graph.commit(state, graph.descendants(state, {arr_ptr, dyn_ptr}));

                        THEN("The output is as expected") {
                            CHECK(std::ranges::equal(
                                    adv_ptr->view(state),
                                    std::vector{0, 1, 2, 3, 20, -1, 22, 23, 40, 41, -2, 43}));
                            // ArrayValidationNode checks most of the consistency etc
                        }
                    }

                    AND_WHEN("We revert") {
                        graph.revert(state, graph.descendants(state, {arr_ptr, dyn_ptr}));

                        THEN("The output is as expected") {
                            CHECK(std::ranges::equal(adv_ptr->view(state), std::vector<double>{}));
                            // ArrayValidationNode checks most of the consistency etc
                        }
                    }
                }
            }
        }

        THEN("We get an exception when accessing the matrix by (:, i, :, j)") {
            CHECK_THROWS(graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, Slice(),
                                                                  j_ptr));
        }

        THEN("We get an exception when accessing the matrix by (:, i, j, :)") {
            CHECK_THROWS(graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, j_ptr,
                                                                  Slice()));
        }
    }

    GIVEN("A non-constant and non-dynamic 1d array and a dynamic indexing array") {
        auto arr_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{10});
        auto i_ptr = graph.emplace_node<DynamicArrayTestingNode>(std::initializer_list<ssize_t>{-1},
                                                                 0, 8, true);

        WHEN("We access the array by i") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr);
            auto validate = graph.emplace_node<ArrayValidationNode>(adv);

            THEN("We get the shape we expect") {
                CHECK(adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{-1}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.empty_state();
                std::vector<double> values(10);
                std::iota(values.begin(), values.end(), 0);
                arr_ptr->initialize_state(state, values);
                graph.initialize_state(state);

                THEN("The state starts empty") {
                    CHECK(std::ranges::equal(adv->view(state), std::vector<double>{}));
                }

                AND_WHEN("We grow the indexing nodes and propagate") {
                    // i_ptr -> {3, 5, 7, 2}
                    i_ptr->grow(state, {3, 5, 7, 2});

                    arr_ptr->propagate(state);
                    i_ptr->propagate(state);
                    adv->propagate(state);
                    validate->propagate(state);

                    THEN("The state has the expected values") {
                        CHECK(std::ranges::equal(adv->view(state), std::vector{3, 5, 7, 2}));
                    }

                    AND_WHEN("We revert the indexing nodes") {
                        arr_ptr->revert(state);
                        i_ptr->revert(state);
                        adv->revert(state);
                        validate->revert(state);

                        THEN("The state goes back to empty") {
                            CHECK(std::ranges::equal(adv->view(state), std::vector<double>{}));
                        }
                    }

                    AND_WHEN("We commit") {
                        arr_ptr->commit(state);
                        i_ptr->commit(state);
                        adv->commit(state);
                        validate->commit(state);

                        THEN("The final state is correct") {
                            CHECK(std::ranges::equal(adv->view(state),
                                                     std::vector<double>{3, 5, 7, 2}));
                        }

                        AND_WHEN("We mutate and shrink the indexing array") {
                            i_ptr->set(state, 2, 6);
                            i_ptr->shrink(state);

                            arr_ptr->propagate(state);
                            i_ptr->propagate(state);
                            adv->propagate(state);
                            validate->propagate(state);

                            THEN("The final state is correct") {
                                CHECK(std::ranges::equal(adv->view(state),
                                                         std::vector<double>{3, 5, 6}));
                            }

                            AND_WHEN("We revert") {
                                arr_ptr->revert(state);
                                i_ptr->revert(state);
                                adv->revert(state);
                                validate->revert(state);

                                THEN("The state goes back to the previous") {
                                    CHECK(std::ranges::equal(adv->view(state),
                                                             std::vector<double>{3, 5, 7, 2}));
                                }
                            }
                        }

                        AND_WHEN("We mutate the main array") {
                            arr_ptr->set_value(state, 3, 103);
                            arr_ptr->set_value(state, 8, 108);
                            arr_ptr->set_value(state, 2, 102);
                            arr_ptr->set_value(state, 5, 105);
                            arr_ptr->set_value(state, 9, 109);

                            arr_ptr->propagate(state);
                            i_ptr->propagate(state);
                            adv->propagate(state);
                            validate->propagate(state);

                            THEN("The state has the expected values") {
                                CHECK(std::ranges::equal(adv->view(state),
                                                         std::vector{103, 105, 7, 102}));
                            }

                            AND_WHEN("We revert") {
                                arr_ptr->revert(state);
                                i_ptr->revert(state);
                                adv->revert(state);
                                validate->revert(state);

                                THEN("The state has the expected values") {
                                    CHECK(std::ranges::equal(adv->view(state),
                                                             std::vector{3, 5, 7, 2}));
                                }
                            }
                        }

                        AND_WHEN("We mutate the main array and the indexer") {
                            arr_ptr->set_value(state, 3, 103);
                            arr_ptr->set_value(state, 8, 108);
                            arr_ptr->set_value(state, 2, 102);
                            arr_ptr->set_value(state, 5, 105);
                            arr_ptr->set_value(state, 9, 109);

                            i_ptr->set(state, 2, 6);     // [3, 5, 6, 2]
                            i_ptr->shrink(state);        // [3, 5, 6]
                            i_ptr->grow(state, {3, 1});  // [3, 5, 6, 3, 1]

                            arr_ptr->propagate(state);
                            i_ptr->propagate(state);
                            adv->propagate(state);
                            validate->propagate(state);

                            THEN("The state has the expected values") {
                                CHECK(std::ranges::equal(adv->view(state),
                                                         std::vector{103, 105, 6, 103, 1}));
                            }

                            AND_WHEN("We revert") {
                                arr_ptr->revert(state);
                                i_ptr->revert(state);
                                adv->revert(state);
                                validate->revert(state);

                                THEN("The state has the expected values") {
                                    CHECK(std::ranges::equal(adv->view(state),
                                                             std::vector{3, 5, 7, 2}));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    GIVEN("A dynamic 4d Nx3x5x4 matrix with 3 non-constant and non-dynamic indexing arrays") {
        auto arr_ptr = graph.emplace_node<DynamicArrayTestingNode>(
                std::initializer_list<ssize_t>{-1, 3, 5, 4});

        auto i_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{3}, 0, 2);
        auto j_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{3}, 0, 4);
        auto k_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{3}, 0, 3);

        WHEN("We access the matrix by (:, i, j, k)") {
            auto adv =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, j_ptr, k_ptr);

            THEN("We get the shape we expect") {
                CHECK(adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{-1, 3}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.empty_state();
                i_ptr->initialize_state(state, {1, 0, 2});
                j_ptr->initialize_state(state, {1, 2, 1});
                k_ptr->initialize_state(state, {0, 0, 2});
                graph.initialize_state(state);

                THEN("The state starts empty") {
                    CHECK(std::ranges::equal(adv->view(state), std::vector<double>{}));
                }

                AND_WHEN("We grow the main array and propagate") {
                    std::vector<double> values(2 * 3 * 5 * 4);
                    std::iota(values.begin(), values.end(), 0);
                    arr_ptr->grow(state, values);

                    arr_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 6);
                        CHECK(std::ranges::equal(adv->view(state),
                                                 std::vector{24, 8, 46, 84, 68, 106}));
                        verify_array_diff({}, {24, 8, 46, 84, 68, 106}, adv->diff(state));
                    }

                    AND_WHEN("We mutate the main array") {
                        arr_ptr->commit(state);
                        adv->commit(state);

                        REQUIRE(adv->diff(state).size() == 0);

                        arr_ptr->set(state, 84, -1);
                        arr_ptr->set(state, 68, -2);
                        arr_ptr->set(state, 2, -3);
                        arr_ptr->set(state, 8, -4);

                        arr_ptr->propagate(state);
                        adv->propagate(state);

                        THEN("The state has the expected values and the diff is correct") {
                            CHECK(adv->size(state) == 6);
                            CHECK(std::ranges::equal(adv->view(state),
                                                     std::vector{24, -4, 46, -1, -2, 106}));
                            verify_array_diff({24, 8, 46, 84, 68, 106}, {24, -4, 46, -1, -2, 106},
                                              adv->diff(state));
                        }
                    }

                    AND_WHEN("We mutate the indexing arrays") {
                        arr_ptr->commit(state);
                        adv->commit(state);

                        REQUIRE(adv->diff(state).size() == 0);

                        arr_ptr->set(state, 84, -1);
                        arr_ptr->set(state, 68, -2);
                        arr_ptr->set(state, 2, -3);
                        arr_ptr->set(state, 8, -4);

                        arr_ptr->propagate(state);
                        adv->propagate(state);

                        THEN("The state has the expected values and the diff is correct") {
                            CHECK(adv->size(state) == 6);
                            CHECK(std::ranges::equal(adv->view(state),
                                                     std::vector{24, -4, 46, -1, -2, 106}));
                            verify_array_diff({24, 8, 46, 84, 68, 106}, {24, -4, 46, -1, -2, 106},
                                              adv->diff(state));
                        }
                    }
                }
            }
        }

        WHEN("We access the matrix by (:, i, j, :)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, j_ptr,
                                                                Slice());
            auto val = graph.emplace_node<ArrayValidationNode>(adv);

            THEN("We get the shape we expect") {
                CHECK(adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{-1, 3, 4}));
            }

            AND_WHEN("We create a state") {
                auto state = graph.empty_state();
                i_ptr->initialize_state(state, {1, 0, 2});
                j_ptr->initialize_state(state, {1, 2, 1});
                graph.initialize_state(state);

                THEN("The state starts empty") {
                    CHECK(std::ranges::equal(adv->view(state), std::vector<double>{}));
                }

                AND_WHEN("We grow the main array and propagate") {
                    std::vector<double> values(2 * 3 * 5 * 4);
                    std::iota(values.begin(), values.end(), 0);
                    arr_ptr->grow(state, values);

                    arr_ptr->propagate(state);
                    adv->propagate(state);
                    val->propagate(state);

                    std::vector<double> expected({24, 25, 26, 27, 8,   9,   10,  11,
                                                  44, 45, 46, 47, 84,  85,  86,  87,
                                                  68, 69, 70, 71, 104, 105, 106, 107});

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 2 * 3 * 4);
                        CHECK(std::ranges::equal(adv->shape(state), std::vector{2, 3, 4}));
                        CHECK(std::ranges::equal(adv->view(state), expected));
                        verify_array_diff({}, expected, adv->diff(state));
                    }

                    arr_ptr->commit(state);
                    adv->commit(state);
                    val->commit(state);

                    AND_WHEN("We mutate the main array") {
                        REQUIRE(adv->diff(state).size() == 0);

                        std::vector<double> new_expected = expected;

                        arr_ptr->set(state, 84, -1);
                        new_expected[12] = -1;

                        arr_ptr->set(state, 68, -2);
                        new_expected[16] = -2;

                        // Outside of the indexed range
                        arr_ptr->set(state, 2, -3);

                        arr_ptr->set(state, 11, -4);
                        new_expected[7] = -4;

                        arr_ptr->propagate(state);
                        adv->propagate(state);
                        val->propagate(state);

                        THEN("The state has the expected values and the diff is correct") {
                            CHECK(adv->size(state) == 2 * 3 * 4);
                            CHECK(std::ranges::equal(adv->shape(state), std::vector{2, 3, 4}));
                            CHECK(std::ranges::equal(adv->view(state), new_expected));
                            verify_array_diff(expected, new_expected, adv->diff(state));
                        }
                    }

                    AND_WHEN("We mutate the indices") {
                        i_ptr->set_value(state, 2, 1);  // 1, 0, 1
                        j_ptr->set_value(state, 1, 1);  // 1, 1, 1

                        std::vector<double> new_expected({24, 25, 26, 27, 4,  5,  6,  7,
                                                          24, 25, 26, 27, 84, 85, 86, 87,
                                                          64, 65, 66, 67, 84, 85, 86, 87});

                        i_ptr->propagate(state);
                        j_ptr->propagate(state);
                        adv->propagate(state);
                        val->propagate(state);

                        THEN("The state has the expected values and the diff is correct") {
                            CHECK(adv->size(state) == 2 * 3 * 4);
                            CHECK(std::ranges::equal(adv->shape(state), std::vector{2, 3, 4}));
                            CHECK(std::ranges::equal(adv->view(state), new_expected));
                            verify_array_diff(expected, new_expected, adv->diff(state));
                        }

                        AND_WHEN("We revert") {
                            i_ptr->revert(state);
                            j_ptr->revert(state);
                            adv->revert(state);
                            THEN("We get back the original state") {
                                // Everything should be checked by the ArrayValidationNode here
                                val->revert(state);
                                CHECK(std::ranges::equal(adv->view(state), expected));
                                CHECK(adv->diff(state).size() == 0);
                            }
                        }
                    }

                    AND_WHEN("We shrink and grow the array and mutate the indices") {
                        arr_ptr->shrink(state);
                        arr_ptr->shrink(state);

                        std::vector<double> values(2 * 3 * 5 * 4);
                        std::iota(values.begin(), values.end(), 1000);
                        arr_ptr->grow(state, values);

                        i_ptr->set_value(state, 2, 1);  // 1, 0, 1
                        j_ptr->set_value(state, 1, 1);  // 1, 1, 1

                        std::vector<double> new_expected({1024, 1025, 1026, 1027, 1004, 1005,
                                                          1006, 1007, 1024, 1025, 1026, 1027,
                                                          1084, 1085, 1086, 1087, 1064, 1065,
                                                          1066, 1067, 1084, 1085, 1086, 1087});

                        i_ptr->propagate(state);
                        j_ptr->propagate(state);
                        adv->propagate(state);
                        val->propagate(state);

                        THEN("The state has the expected values and the diff is correct") {
                            CHECK(adv->size(state) == 2 * 3 * 4);
                            CHECK(std::ranges::equal(adv->shape(state), std::vector{2, 3, 4}));
                            CHECK(std::ranges::equal(adv->view(state), new_expected));
                            verify_array_diff(expected, new_expected, adv->diff(state));
                        }

                        AND_WHEN("We revert") {
                            i_ptr->revert(state);
                            j_ptr->revert(state);
                            adv->revert(state);
                            THEN("We get back the original state") {
                                // Everything should be checked by the ArrayValidationNode here
                                val->revert(state);
                                CHECK(std::ranges::equal(adv->view(state), expected));
                                CHECK(adv->diff(state).size() == 0);
                            }
                        }
                    }
                }
            }
        }

        THEN("We get an exception when accessing the matrix by (:, i, :, j)") {
            CHECK_THROWS(graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, Slice(),
                                                                  j_ptr));
        }
    }

    GIVEN("A static-sized 4d 2x3x5x4 matrix with 3 non-constant scalar indices") {
        auto arr_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{2, 3, 5, 4},
                                                       -1000, 1000);

        auto i_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{}, 0, 1);
        auto j_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{}, 0, 2);
        auto k_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{}, 0, 3);

        WHEN("We access the matrix by (i, j, :, :)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, j_ptr, Slice(),
                                                                Slice());

            THEN("We get the shape we expect") {
                CHECK(!adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{5, 4}));
            }

            AND_WHEN("We create a state") {
                std::vector<double> values(2 * 3 * 5 * 4);
                std::iota(values.begin(), values.end(), 0);

                auto state = graph.empty_state();
                arr_ptr->initialize_state(state, values);
                i_ptr->initialize_state(state, {1});
                j_ptr->initialize_state(state, {1});
                graph.initialize_state(state);

                std::vector<double> expected_initial_state({80, 81, 82, 83, 84, 85, 86,
                                                            87, 88, 89, 90, 91, 92, 93,
                                                            94, 95, 96, 97, 98, 99});

                THEN("The state starts with the expected values") {
                    CHECK(std::ranges::equal(adv->view(state), expected_initial_state));
                }

                AND_WHEN("We mutate i and j") {
                    i_ptr->set_value(state, 0, 0);
                    j_ptr->set_value(state, 0, 2);

                    i_ptr->propagate(state);
                    j_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 20);

                        std::vector<double> new_state({40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                                       50, 51, 52, 53, 54, 55, 56, 57, 58, 59});
                        CHECK(std::ranges::equal(adv->view(state), new_state));
                        verify_array_diff(expected_initial_state, new_state, adv->diff(state));
                    }
                }

                AND_WHEN("We mutate the main array") {
                    arr_ptr->set_value(state, 80, 80);
                    arr_ptr->set_value(state, 81, -81);

                    arr_ptr->set_value(state, 79, 79);
                    arr_ptr->set_value(state, 78, -78);

                    arr_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 20);

                        std::vector<double> new_state({80, -81, 82, 83, 84, 85, 86, 87, 88, 89,
                                                       90, 91,  92, 93, 94, 95, 96, 97, 98, 99});
                        CHECK(std::ranges::equal(adv->view(state), new_state));
                        verify_array_diff(expected_initial_state, new_state, adv->diff(state));
                    }
                }
            }
        }

        WHEN("We access the matrix by (:, i, j, :)") {
            auto adv = graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, j_ptr,
                                                                Slice());

            THEN("We get the shape we expect") {
                CHECK(!adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{2, 4}));
            }

            AND_WHEN("We create a state") {
                std::vector<double> values(2 * 3 * 5 * 4);
                std::iota(values.begin(), values.end(), 0);

                auto state = graph.empty_state();
                arr_ptr->initialize_state(state, values);
                i_ptr->initialize_state(state, {1});
                j_ptr->initialize_state(state, {1});
                graph.initialize_state(state);

                std::vector<double> expected_initial_state({24, 25, 26, 27, 84, 85, 86, 87});

                THEN("The state starts with the expected values") {
                    CHECK(std::ranges::equal(adv->view(state), expected_initial_state));
                }

                AND_WHEN("We mutate i and j") {
                    i_ptr->set_value(state, 0, 0);
                    j_ptr->set_value(state, 0, 2);

                    i_ptr->propagate(state);
                    j_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 8);

                        std::vector<double> new_state({8, 9, 10, 11, 68, 69, 70, 71});
                        CHECK(std::ranges::equal(adv->view(state), new_state));
                        verify_array_diff(expected_initial_state, new_state, adv->diff(state));
                    }
                }

                AND_WHEN("We mutate the main array") {
                    arr_ptr->set_value(state, 24, 24);
                    arr_ptr->set_value(state, 25, -25);

                    arr_ptr->set_value(state, 23, 23);
                    arr_ptr->set_value(state, 22, -22);

                    arr_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 8);

                        std::vector<double> new_state({24, -25, 26, 27, 84, 85, 86, 87});
                        CHECK(std::ranges::equal(adv->view(state), new_state));
                        verify_array_diff(expected_initial_state, new_state, adv->diff(state));
                    }
                }
            }
        }

        WHEN("We access the matrix by (i, j, :, k)") {
            auto adv =
                    graph.emplace_node<AdvancedIndexingNode>(arr_ptr, i_ptr, j_ptr, Slice(), k_ptr);

            THEN("We get the shape we expect") {
                CHECK(!adv->dynamic());
                CHECK(std::ranges::equal(adv->shape(), std::vector{5}));
            }

            AND_WHEN("We create a state") {
                std::vector<double> values(2 * 3 * 5 * 4);
                std::iota(values.begin(), values.end(), 0);

                auto state = graph.empty_state();
                arr_ptr->initialize_state(state, values);
                i_ptr->initialize_state(state, {1});
                j_ptr->initialize_state(state, {1});
                k_ptr->initialize_state(state, {3});
                graph.initialize_state(state);

                std::vector<double> expected_initial_state({83, 87, 91, 95, 99});

                THEN("The state starts with the expected values") {
                    CHECK(std::ranges::equal(adv->view(state), expected_initial_state));
                }

                AND_WHEN("We mutate i and j") {
                    i_ptr->set_value(state, 0, 0);
                    j_ptr->set_value(state, 0, 2);
                    k_ptr->set_value(state, 0, 1);

                    i_ptr->propagate(state);
                    j_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 5);

                        std::vector<double> new_state({41, 45, 49, 53, 57});
                        CHECK(std::ranges::equal(adv->view(state), new_state));
                        verify_array_diff(expected_initial_state, new_state, adv->diff(state));
                    }
                }

                AND_WHEN("We mutate the main array") {
                    arr_ptr->set_value(state, 83, 83);
                    arr_ptr->set_value(state, 87, -87);

                    arr_ptr->set_value(state, 82, 82);
                    arr_ptr->set_value(state, 81, -81);

                    arr_ptr->propagate(state);
                    adv->propagate(state);

                    THEN("The state has the expected values and the diff is correct") {
                        CHECK(adv->size(state) == 5);

                        std::vector<double> new_state({83, -87, 91, 95, 99});
                        CHECK(std::ranges::equal(adv->view(state), new_state));
                        verify_array_diff(expected_initial_state, new_state, adv->diff(state));
                    }
                }
            }
        }
    }

    GIVEN("A static-sized 4d 2x3x5x4 matrix with a 2d indexing array") {
        auto arr_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{2, 3, 5, 4},
                                                       -1000, 1000);

        auto i_ptr = graph.emplace_node<IntegerNode>(std::initializer_list<ssize_t>{2, 3}, 0, 1);

        THEN("We are prevented from doing an indexing operation") {
            CHECK_THROWS(graph.emplace_node<AdvancedIndexingNode>(arr_ptr, Slice(), i_ptr, Slice(),
                                                                  Slice()));
        }
    }
}

}  // namespace dwave::optimization