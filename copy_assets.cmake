
function(copy_assets src_folder target_folder)
    message("Relative path ${target_folder}")
    message("Copying from: ${src_folder} to: ${target_folder}")
    file(COPY ${src_folder} DESTINATION ${target_folder})
endfunction()

copy_assets(${SRC_FOLDER} ${TARGET_FOLDER})