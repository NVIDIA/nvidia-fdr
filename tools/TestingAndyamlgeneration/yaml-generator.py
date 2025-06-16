# Define the block of text with a placeholder for the count

#example block
block_of_text = """
    - ID: PORT-XMIT-DATA[{count}]
      DataType: Uint64
      DbusParams:
        Interface: xyz.openbmc_project.Metrics.PortMetricsOem2
        ObjectPath: /xyz/openbmc_project/inventory/system/processors/GPU_SXM_$gpuid/Ports/NVLink_{count}
        Property: TXBytes
        Service: xyz.openbmc_project.NSM
      FetchFreqSecs: 600
      FetchMethod: Dbus
      FetchType: Poll
      ParamID: 3
      StoreFreqSecs: 600
      StorePolicy: OnChange

"""

# Open a file to write the output
with open("output.txt", "w") as file:
    # Loop 72 times to generate the text
    for count in range(0, 18):
        # Write the block of text with the current count
        file.write(block_of_text.format(count=count))

print("File generated successfully!")