# threadmaxx_ui test suite (UI1+).
#
# Each test links `threadmaxx::ui` and uses the tiny `Check.hpp` harness
# from the parent `tests/` directory. Registered with CTest under the
# `ui.*` name prefix.

set(THREADMAXX_UI_TESTS
    test_ui_widget_id
    test_ui_rect_math
    test_ui_draw_list
    test_ui_context_lifecycle
    test_ui_no_allocations
    test_ui_layout_row
    test_ui_layout_column
    test_ui_layout_nested
    test_ui_layout_padding_spacing
    test_ui_layout_clipping
    test_ui_layout_overflow
    test_ui_input_hover
    test_ui_input_focus
    test_ui_input_capture_mouse
    test_ui_input_capture_keyboard
    test_ui_input_determinism
    test_ui_widget_button
    test_ui_widget_checkbox
    test_ui_widget_radio
    test_ui_widget_slider
    test_ui_widget_drag_scalar
    test_ui_widget_input_text
    test_ui_widget_selectable
    test_ui_widget_tooltip
    test_ui_widget_no_allocations
    test_ui_tree_expand
    test_ui_tree_keyboard_nav
    test_ui_menu_bar
    test_ui_popup_modal
    test_ui_menu_keyboard_nav
    test_ui_inspect_primitives
    test_ui_inspect_enum
    test_ui_inspect_vector
    test_ui_inspect_handle
    test_ui_inspect_no_allocations
)

foreach(test ${THREADMAXX_UI_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::ui)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME ui.${test} COMMAND ${test})
endforeach()
